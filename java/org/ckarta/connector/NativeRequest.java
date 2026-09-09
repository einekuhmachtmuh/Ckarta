package org.ckarta.connector;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/** Thin request facade over native request metadata and data views. */
public final class NativeRequest
{
	private final long handle;
	private final ByteBuffer metadata;
	private final ByteBuffer data;
	private final int methodLength;
	private final int targetLength;
	private final int protocolLength;

	public NativeRequest(long handle, ByteBuffer data)
	{
		this(handle, ByteBuffer.allocateDirect(0), data);
	}

	public NativeRequest(long handle, ByteBuffer metadata, ByteBuffer data)
	{
		if (handle == 0)
		{
			throw new IllegalArgumentException("request handle must be non-zero");
		}
		if (metadata == null)
		{
			throw new NullPointerException("request metadata");
		}
		if (data == null)
		{
			throw new NullPointerException("request data");
		}
		if (!metadata.isDirect() || !data.isDirect())
		{
			throw new IllegalArgumentException(
					"native request views must be direct buffers");
		}

		ByteBuffer header = metadata.asReadOnlyBuffer().order(ByteOrder.BIG_ENDIAN);
		if (header.remaining() == 0)
		{
			methodLength = 0;
			targetLength = 0;
			protocolLength = 0;
			this.handle = handle;
			this.metadata = metadata.asReadOnlyBuffer();
			this.data = data.asReadOnlyBuffer();
			return;
		}
		if (header.remaining() < 13)
		{
			throw new IllegalArgumentException("invalid native request metadata");
		}

		methodLength = header.getInt();
		targetLength = header.getInt();
		protocolLength = header.getInt();
		if (methodLength < 0 || targetLength < 0 || protocolLength < 0
				|| 13L + methodLength + targetLength + protocolLength
					!= header.remaining())
		{
			throw new IllegalArgumentException("invalid native request metadata");
		}

		this.handle = handle;
		this.metadata = metadata.asReadOnlyBuffer();
		this.data = data.asReadOnlyBuffer();
	}

	public long handle()
	{
		return handle;
	}

	public ByteBuffer metadata()
	{
		return metadata.asReadOnlyBuffer();
	}

	public ByteBuffer data()
	{
		return data.asReadOnlyBuffer();
	}

	public ByteBuffer methodBytes()
	{
		requireMetadata();
		return metadataSlice(13, methodLength);
	}

	public ByteBuffer targetBytes()
	{
		requireMetadata();
		return metadataSlice(13 + methodLength, targetLength);
	}

	public ByteBuffer protocolBytes()
	{
		requireMetadata();
		return metadataSlice(13 + methodLength + targetLength, protocolLength);
	}

	public boolean connectionCloseRequired()
	{
		requireMetadata();
		ByteBuffer view = metadata.asReadOnlyBuffer();
		view.position(12 + 0 + methodLength + targetLength + protocolLength);
		return view.get() != 0;
	}

	private void requireMetadata()
	{
		if (metadata.remaining() == 0)
		{
			throw new IllegalStateException("request metadata is unavailable");
		}
	}

	private ByteBuffer metadataSlice(int offset, int length)
	{
		ByteBuffer view = metadata.asReadOnlyBuffer();
		view.position(offset);
		view.limit(offset + length);
		return view.slice().asReadOnlyBuffer();
	}
}
