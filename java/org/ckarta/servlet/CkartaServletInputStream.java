package org.ckarta.servlet;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.Objects;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicBoolean;

import jakarta.servlet.ReadListener;
import jakarta.servlet.ServletInputStream;

/**
 * Minimal Servlet 6.1 input-stream semantic adapter over a container-owned
 * request-body source.
 *
 * <p>The source is deliberately abstract: native queue ownership, connection
 * cancellation and readiness notification remain outside the application API
 * and are supplied by the container integration layer.</p>
 */
public final class CkartaServletInputStream extends ServletInputStream
{
	interface BodySource extends AutoCloseable
	{
		int DATA = 0;
		int WOULD_BLOCK = 1;
		int EOF = 2;
		int ERROR = 3;

		int read(ByteBuffer destination, boolean blocking) throws IOException;

		boolean isReady();

		boolean isFinished();

		void setReadInterest(Runnable onReady);

		void setErrorInterest(Runnable onError);

		@Override
		void close() throws IOException;
	}

	private final BodySource source;
	private final Executor callbackExecutor;
	private final boolean asyncStarted;
	private final AtomicBoolean listenerCallbackScheduled = new AtomicBoolean();
	private volatile ReadListener listener;
	private volatile boolean nonBlocking;
	private volatile boolean readyPermit;
	private volatile boolean waitingForData;
	private volatile boolean closed;
	private volatile boolean callbackActive;
	private volatile boolean allDataReadNotified;
	private volatile IOException terminalError;

	public CkartaServletInputStream(
			BodySource source,
			Executor callbackExecutor,
			boolean asyncStarted)
	{
		this.source = Objects.requireNonNull(source, "source");
		this.callbackExecutor = Objects.requireNonNull(callbackExecutor, "callbackExecutor");
		this.asyncStarted = asyncStarted;
	}

	@Override
	public boolean isFinished()
	{
		if (closed)
		{
			return true;
		}
		return source.isFinished();
	}

	@Override
	public boolean isReady()
	{
		if (closed || terminalError != null)
		{
			return false;
		}
		if (!nonBlocking)
		{
			return true;
		}
		if (source.isFinished())
		{
			readyPermit = false;
			waitingForData = false;
			return true;
		}

		boolean ready = source.isReady();
		readyPermit = ready;
		waitingForData = !ready;
		return ready;
	}

	@Override
	public void setReadListener(ReadListener readListener)
	{
		Objects.requireNonNull(readListener, "readListener");
		if (!asyncStarted)
		{
			throw new IllegalStateException(
					"ReadListener requires async processing");
		}
		if (listener != null)
		{
			throw new IllegalStateException(
					"ReadListener is already set");
		}
		if (closed || terminalError != null)
		{
			throw new IllegalStateException("input stream is not active");
		}

		listener = readListener;
		nonBlocking = true;
		source.setReadInterest(this::scheduleDataAvailable);
		source.setErrorInterest(this::scheduleError);
	
		if (source.isFinished())
		{
			scheduleAllDataRead();
		}
		else if (source.isReady())
		{
			scheduleDataAvailable();
		}
	}

	@Override
	public int read() throws IOException
	{
		byte[] one = new byte[1];
		int count = read(one, 0, 1);
		return count == -1 ? -1 : one[0] & 0xff;
	}

	@Override
	public int read(byte[] b, int off, int len) throws IOException
	{
		Objects.requireNonNull(b, "b");
		if (off < 0 || len < 0 || off > b.length - len)
		{
			throw new IndexOutOfBoundsException();
		}
		if (len == 0)
		{
			return 0;
		}
		return read(ByteBuffer.wrap(b, off, len));
	}

	@Override
	public int read(ByteBuffer buffer) throws IOException
	{
		Objects.requireNonNull(buffer, "buffer");
		if (closed)
		{
			throw new IOException("input stream is closed");
		}
		if (!buffer.hasRemaining())
		{
			return 0;
		}
		checkReadPermission();

		int result = source.read(buffer, !nonBlocking);
		switch (result)
		{
			case BodySource.DATA ->
			{
				readyPermit = callbackActive;
				if (source.isFinished())
				{
					readyPermit = false;
					waitingForData = false;
				}
				else if (!callbackActive)
				{
					readyPermit = false;
				}
				return buffer.position();
			}
			case BodySource.EOF ->
			{
				readyPermit = false;
				waitingForData = false;
				scheduleAllDataRead();
				return -1;
			}
			case BodySource.WOULD_BLOCK ->
			{
				readyPermit = false;
				waitingForData = true;
				if (nonBlocking)
				{
					throw new IllegalStateException(
							"read became non-ready during non-blocking read");
				}
				throw new IOException("blocking body source unexpectedly would block");
			}
			case BodySource.ERROR ->
			{
				IOException error = terminalError;
				if (error == null)
				{
					error = new IOException("request body read failed");
					terminalError = error;
				}
				throw error;
			}
			default -> throw new IOException("invalid body source result: " + result);
		}
	}

	void notifyBodyError(Throwable error)
	{
		if (error == null)
		{
			terminalError = new IOException("request body error");
		}
		else if (error instanceof IOException ioException)
		{
			terminalError = ioException;
		}
		else
		{
			terminalError = new IOException("request body error", error);
		}
		scheduleError();
	}

	private void checkReadPermission()
	{
		if (!nonBlocking)
		{
			return;
		}
		if (callbackActive)
		{
			return;
		}
		if (!readyPermit)
		{
			throw new IllegalStateException(
					"non-blocking read requires isReady() to return true");
		}
	}

	private void scheduleDataAvailable()
	{
		if (closed || listener == null || !waitingForData && !listenerCallbackScheduled.compareAndSet(false, true))
		{
			if (closed || listener == null)
			{
				return;
			}
			if (!waitingForData)
			{
				return;
			}
			if (!listenerCallbackScheduled.compareAndSet(false, true))
			{
				return;
			}
		}
		listenerCallbackScheduled.set(true);
		callbackExecutor.execute(this::invokeDataAvailable);
	}

	private void invokeDataAvailable()
	{
		try
		{
			if (closed || listener == null || terminalError != null)
			{
				return;
			}
			waitingForData = false;
			callbackActive = true;
			readyPermit = true;
			listener.onDataAvailable();
		}
		catch (Throwable error)
		{
			notifyBodyError(error);
		}
		finally
		{
			callbackActive = false;
			readyPermit = false;
			listenerCallbackScheduled.set(false);
			if (source.isFinished() && terminalError == null)
			{
				scheduleAllDataRead();
			}
		}
	}

	private void scheduleAllDataRead()
	{
		if (closed || listener == null || allDataReadNotified)
		{
			return;
		}
		allDataReadNotified = true;
		callbackExecutor.execute(() ->
			{
				try
				{
					listener.onAllDataRead();
				}
				catch (Throwable error)
				{
					notifyBodyError(error);
				}
			});
	}

	private void scheduleError()
	{
		if (closed || listener == null)
		{
			return;
		}
		callbackExecutor.execute(() ->
			{
				Throwable error = terminalError;
				if (error == null)
				{
					error = new IOException("request body error");
				}
				try
				{
					listener.onError(error);
				}
				catch (Throwable ignored)
				{
					/* Terminal error remains authoritative. */
				}
			});
	}

	@Override
	public void close() throws IOException
	{
		if (!closed)
		{
			closed = true;
			readyPermit = false;
			waitingForData = false;
			source.close();
		}
	}
}
