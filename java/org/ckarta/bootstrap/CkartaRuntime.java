package org.ckarta.bootstrap;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.AtomicBoolean;
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
	private static final int COMPLETION_QUEUE_CAPACITY = 16;

	private static ThreadPoolExecutor executor;
	private static ArrayBlockingQueue<CompletionRecord> completions;
	private static AtomicBoolean completionOverflow;

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
				completions = new ArrayBlockingQueue<>(COMPLETION_QUEUE_CAPACITY);
				completionOverflow = new AtomicBoolean(false);
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
			completions = null;
			completionOverflow = null;
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

	public static void dispatchAsync(long requestHandle, long ownerToken,
		long lifetimeToken, ByteBuffer data)
	{
		ThreadPoolExecutor currentExecutor;
		ArrayBlockingQueue<CompletionRecord> currentCompletions;
		AtomicBoolean currentOverflow;
		synchronized (EXECUTOR_LOCK)
		{
			currentExecutor = executor;
			currentCompletions = completions;
			currentOverflow = completionOverflow;
		}

		if (currentExecutor == null || currentCompletions == null)
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

				if (!currentCompletions.offer(
						new CompletionRecord(requestHandle, ownerToken, lifetimeToken, result, status)))
				{
					if (currentOverflow != null)
					{
						currentOverflow.set(true);
					}
				}
			});
		}
		catch (RejectedExecutionException exception)
		{
			if (!currentCompletions.offer(
					new CompletionRecord(requestHandle, ownerToken, lifetimeToken, 0L, -2))
				&& currentOverflow != null)
			{
				currentOverflow.set(true);
			}
		}
	}

	public static int pollCompletion(ByteBuffer output)
	{
		if (output == null || !output.isDirect() || output.capacity() < 36)
		{
			throw new IllegalArgumentException("completion output buffer");
		}

		ArrayBlockingQueue<CompletionRecord> currentCompletions;
		synchronized (EXECUTOR_LOCK)
		{
			currentCompletions = completions;
		}

		AtomicBoolean currentOverflow;
		synchronized (EXECUTOR_LOCK)
		{
			currentOverflow = completionOverflow;
		}

		if (currentCompletions == null || currentOverflow == null)
		{
			return -1;
		}

		if (currentOverflow.get())
		{
			return -2;
		}

		CompletionRecord completion = currentCompletions.poll();
		if (completion == null)
		{
			return 0;
		}

		output.order(ByteOrder.nativeOrder());
		output.putLong(0, completion.requestHandle());
		output.putLong(8, completion.ownerToken());
		output.putLong(16, completion.lifetimeToken());
		output.putLong(24, completion.result());
		output.putInt(32, completion.status());
		return 1;
	}

	private record CompletionRecord(long requestHandle, long ownerToken,
		long lifetimeToken, long result, int status)
	{
	}
}
