package org.ckarta.bootstrap;

import java.nio.ByteBuffer;
import java.util.concurrent.ArrayBlockingQueue;
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
	private static long nativeQueueHandle;

	private CkartaRuntime()
	{
	}

	public static void start(long queueHandle)
	{
		if (queueHandle <= 0)
		{
			throw new IllegalArgumentException("invalid native completion queue");
		}

		synchronized (EXECUTOR_LOCK)
		{
			if (executor == null)
			{
				nativeQueueHandle = queueHandle;
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

		synchronized (EXECUTOR_LOCK)
		{
			nativeQueueHandle = 0;
		}
		System.out.println("CKARTA_JAVA_STOP");
	}

	public static void dispatchAsync(long requestHandle, long ownerToken,
		long lifetimeToken, ByteBuffer data)
	{
		ThreadPoolExecutor currentExecutor;
		long queueHandle;
		synchronized (EXECUTOR_LOCK)
		{
			currentExecutor = executor;
			queueHandle = nativeQueueHandle;
		}

		if (currentExecutor == null || queueHandle <= 0)
		{
			throw new IllegalStateException("Ckarta runtime is not running");
		}

		try
		{
			currentExecutor.execute(() ->
			{
				long result = 0L;
				int status = 0;
				try
				{
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

				int publishStatus = publishCompletion(queueHandle,
						requestHandle, ownerToken, lifetimeToken, 1L, result, status);
				if (publishStatus != 0 && publishStatus != 2)
				{
					throw new IllegalStateException(
							"native completion publication failed: " + publishStatus);
				}
			});
		}
		catch (RejectedExecutionException exception)
		{
			int publishStatus = publishCompletion(queueHandle,
					requestHandle, ownerToken, lifetimeToken, 0L, -2);
			if (publishStatus != 0 && publishStatus != 2)
			{
				throw new IllegalStateException(
						"native rejection publication failed: " + publishStatus);
			}
		}
	}

	private static native int publishCompletion(long queueHandle,
		long requestHandle, long ownerToken, long lifetimeToken,
		long cycleId, long result, int status);
}
