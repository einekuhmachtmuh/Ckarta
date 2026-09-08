package org.ckarta.bootstrap;

import java.nio.ByteBuffer;

import org.ckarta.connector.NativeRequest;

/** Minimal Java-side bootstrap boundary used by the native startup smoke test. */
public final class CkartaRuntime
{
	private static volatile boolean running;

	private CkartaRuntime()
	{
	}

	public static void start()
	{
		running = true;
		System.out.println("CKARTA_JAVA_READY");
	}

	public static void stop()
	{
		running = false;
		System.out.println("CKARTA_JAVA_STOP");
	}

	public static long dispatch(long requestHandle, ByteBuffer data)
	{
		if (!running)
		{
			throw new IllegalStateException("Ckarta runtime is not running");
		}

		NativeRequest request = new NativeRequest(requestHandle, data);
		if (!request.data().isDirect())
		{
			throw new IllegalArgumentException("native request data must be direct");
		}

		return request.handle() + request.data().remaining();
	}
}
