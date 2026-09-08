package org.ckarta.bootstrap;

import java.nio.ByteBuffer;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Future;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

import org.ckarta.connector.NativeRequest;

/**
 * Minimal Java-side bootstrap boundary used by the native startup smoke test.
 */
public final class CkartaRuntime
{
	private static final Object EXECUTOR_LOCK = new Object();
	private static final int EXECUTOR_QUEUE_CAPACITY = 16;

	private static ThreadPoolExecutor executor;

	private static native int nativeComplete(long completionToken, long requestHandle,
			long result, int status);

	private CkartaRuntime()
	{
	}

	public static void start()
	{
		synchronized (EXECUTOR_LOCK)
		{
			if (executor == null)
			{
				executor = new ThreadPoolExecutor(
						1,
						1,
						0L,
						TimeUnit.MILLISECONDS,
						new ArrayBlockingQueue<>(EXECUTOR_QUEUE_CAPACITY),
						new ThreadPoolExecutor.AbortPolicy());
			}
		}
		System.out.println("CKARTA_JAVA_READY");
	}

	public static void stop()
	{
		ThreadPoolExecutor currentExecutor;
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

	public static void dispatchAsync(long requestHandle, ByteBuffer data,
			long completionToken)
	{
		ThreadPoolExecutor currentExecutor;
		synchronized (EXECUTOR_LOCK)
		{
			currentExecutor = executor;
		}

		if (currentExecutor == null)
		{
			throw new IllegalStateException("Ckarta runtime is not running");
		}

		final long callerThreadId = Thread.currentThread().getId();
		try
		{
			currentExecutor.execute(() ->
			{
				long result = 0L;
				int status = 0;

				try
				{
					if (Thread.currentThread().getId() == callerThreadId)
					{
						throw new IllegalStateException(
								"Servlet execution remained on JNI caller thread");
					}

					NativeRequest request = new NativeRequest(requestHandle, data);
					if (!request.data().isDirect())
					{
						throw new IllegalArgumentException(
								"native request data must be direct");
					}

					result = request.handle() + request.data().remaining();
				}
				catch (RuntimeException exception)
				{
					status = -1;
				}
				finally
				{
					nativeComplete(completionToken, requestHandle, result, status);
				}
			});
		}
		catch (RejectedExecutionException exception)
		{
			nativeComplete(completionToken, requestHandle, 0L, -2);
		}
	}

	public static long dispatch(long requestHandle, ByteBuffer data)
	{
		ThreadPoolExecutor currentExecutor;
		synchronized (EXECUTOR_LOCK)
		{
			currentExecutor = executor;
		}

		if (currentExecutor == null)
		{
			throw new IllegalStateException("Ckarta runtime is not running");
		}

		final long callerThreadId = Thread.currentThread().getId();
		final Future<Long> completion;
		try
		{
			completion = currentExecutor.submit(() ->
			{
				if (Thread.currentThread().getId() == callerThreadId)
				{
					throw new IllegalStateException(
							"Servlet execution remained on JNI caller thread");
				}

				NativeRequest request = new NativeRequest(requestHandle, data);
				if (!request.data().isDirect())
				{
					throw new IllegalArgumentException(
							"native request data must be direct");
				}

				return request.handle() + request.data().remaining();
			});
		}
		catch (RejectedExecutionException exception)
		{
			throw new IllegalStateException("Java request executor is saturated",
					exception);
		}

		try
		{
			/*
			 * This blocking wait is only part of the executable smoke slice.
			 * The production C event loop must use a non-blocking completion path.
			 */
			return completion.get();
		}
		catch (InterruptedException exception)
		{
			completion.cancel(true);
			Thread.currentThread().interrupt();
			throw new IllegalStateException(
					"Interrupted while awaiting Java request completion", exception);
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
