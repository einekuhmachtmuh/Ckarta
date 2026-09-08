package org.ckarta.connector;

import java.nio.ByteBuffer;

/** Thin request facade over the native request handle and native data view. */
public final class NativeRequest
{
	private final long handle;
	private final ByteBuffer data;

	public NativeRequest(long handle, ByteBuffer data)
	{
		if (handle == 0)
		{
			throw new IllegalArgumentException("request handle must be non-zero");
		}
		if (data == null)
		{
			throw new NullPointerException("request data");
		}

		this.handle = handle;
		this.data = data.asReadOnlyBuffer();
	}

	public long handle()
	{
		return handle;
	}

	public ByteBuffer data()
	{
		return data.asReadOnlyBuffer();
	}
}
