package org.ckarta.bootstrap;

import java.nio.ByteBuffer;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;

import org.ckarta.connector.NativeRequest;

/** Minimal Java-side bootstrap boundary used by the native startup smoke test. */
public final class CkartaRuntime
{
	private static final Object EXECUTOR_LOCK = new Object();

	private static ExecutorService executor;

	private CkartaRuntime()
	{
	}

	public static void start()
	{
		synchronized (EXECUTOR_LOCK)
		{
			if (executor == null)
			{
				executor = Executors.newFixedThreadPool(1);
			}
		}
		System.out.println("CKARTA_JAVA_READY");
	}

	public static void stop()
	{
		ExecutorService currentExecutor;
		synchronized (EXECUTOR_LOCK)
		{
			currentExecutor = executor;
			executor = null;
		}

		if (currentExecutor != null)
		{
			currentExecutor.shutdown();
			try
			{
				if (!currentExecutor.awaitTermination(5, TimeUnit.SECONDS))
				{
					currentExecutor.shutdownNow();
				}
			}
			catch (InterruptedException exception)
			{
				currentExecutor.shutdownNow();
				Thread.currentThread().interrupt();
			}
		}
		System.out.println("CKARTA_JAVA_STOP");
	}

	public static long dispatch(long requestHandle, ByteBuffer data)
	{
		ExecutorService currentExecutor;
		synchronized (EXECUTOR_LOCK)
		{
			currentExecutor = executor;
		}

		if (currentExecutor == null)
		{
			throw new IllegalStateException("Ckarta runtime is not running");
		}

		final long callerThreadId = Thread.currentThread().getId();
		Future<Long> completion = currentExecutor.submit(() ->
		{
			if (Thread.currentThread().getId() == callerThreadId)
			{
				throw new IllegalStateException("Servlet execution remained on JNI caller thread");
			}

			NativeRequest request = new NativeRequest(requestHandle, data);
			if (!request.data().isDirect())
			{
				throw new IllegalArgumentException("native request data must be direct");
			}

			return request.handle() + request.data().remaining();
		});

		try
		{
			return completion.get();
		}
		catch (InterruptedException exception)
		{
			completion.cancel(true);
			Thread.currentThread().interrupt();
			throw new IllegalStateException("Interrupted while awaiting Java request completion", exception);
		}
		catch (ExecutionException exception)
		{
			Throwable cause = exception.getCause();
			if (cause instanceof RuntimeException runtimeException)
			{
				throw runtimeException;
			}
			throw new IllegalStateException("Java request execution failed", cause);
		}
	}
}
