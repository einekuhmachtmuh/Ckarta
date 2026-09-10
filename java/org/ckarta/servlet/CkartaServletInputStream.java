package org.ckarta.servlet;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.Objects;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Consumer;

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

		/**
		 * Registers a level-triggered readiness callback. If the source is already
		 * ready when this method is called, the callback must be invoked or a
		 * subsequent isReady() observation must make the condition visible.
		 */
		void setReadInterest(Runnable onReady);

		/**
		 * Registers a callback carrying the source's terminal error. The callback
		 * must be invoked at most once for a given terminal source state.
		 */
		void setErrorInterest(Consumer<Throwable> onError);

		@Override
		void close() throws IOException;
	}

	private final BodySource source;
	private final Executor callbackExecutor;
	private final boolean asyncStarted;
	private final AtomicBoolean callbackScheduled = new AtomicBoolean();
	private final AtomicBoolean allDataReadNotified = new AtomicBoolean();
	private final AtomicBoolean errorNotified = new AtomicBoolean();
	private final AtomicReference<ReadListener> listener = new AtomicReference<>();
	private final AtomicReference<IOException> terminalError = new AtomicReference<>();
	private final Object listenerLock = new Object();
	private volatile boolean nonBlocking;
	private volatile boolean readyPermit;
	private volatile boolean waitingForData;
	private volatile boolean closed;
	private volatile boolean callbackActive;

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
		return closed || source.isFinished();
	}

	@Override
	public boolean isReady()
	{
		if (closed || terminalError.get() != null)
		{
			return false;
		}
		if (!nonBlocking)
		{
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
			throw new IllegalStateException("ReadListener requires async processing");
		}
		if (closed || terminalError.get() != null)
		{
			throw new IllegalStateException("input stream is not active");
		}

		synchronized (listenerLock)
		{
			if (!this.listener.compareAndSet(null, readListener))
			{
				throw new IllegalStateException("ReadListener is already set");
			}
			nonBlocking = true;
			boolean finished = source.isFinished();
			boolean ready = !finished && source.isReady();
			waitingForData = !finished && !ready;

			source.setReadInterest(() -> scheduleDataAvailable(false));
			source.setErrorInterest(this::notifyBodyError);

			if (finished || (!ready && source.isFinished()))
			{
				scheduleAllDataRead();
			}
			else if (ready || source.isReady())
			{
				scheduleDataAvailable(true);
			}
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
		int initialPosition = buffer.position();
		if (!buffer.hasRemaining())
		{
			return 0;
		}
		checkReadPermission();

		ByteBuffer destination = buffer.duplicate();
		int result;
		try
		{
			result = source.read(destination, !nonBlocking);
		}
		catch (IOException error)
		{
			notifyBodyError(error);
			throw error;
		}
		catch (RuntimeException error)
		{
			notifyBodyError(error);
			throw error;
		}

		switch (result)
		{
			case BodySource.DATA ->
			{
				int bytesRead = destination.position() - initialPosition;
				if (bytesRead <= 0 || bytesRead > buffer.remaining())
				{
					throw new IOException("body source returned invalid read length");
				}
				buffer.limit(initialPosition + bytesRead);
				if (!callbackActive)
				{
					readyPermit = false;
				}
				return bytesRead;
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
				IOException error = terminalError.get();
				if (error == null)
				{
					notifyBodyError(null);
					error = terminalError.get();
				}
				throw error;
			}
			default -> throw new IOException("invalid body source result: " + result);
		}
	}

	@Override
	public int readLine(byte[] b, int off, int len) throws IOException
	{
		checkBlockingOnly();
		return super.readLine(b, off, len);
	}

	@Override
	public byte[] readAllBytes() throws IOException
	{
		checkBlockingOnly();
		return super.readAllBytes();
	}

	@Override
	public int readNBytes(byte[] b, int off, int len) throws IOException
	{
		checkBlockingOnly();
		return super.readNBytes(b, off, len);
	}

	@Override
	public byte[] readNBytes(int len) throws IOException
	{
		checkBlockingOnly();
		return super.readNBytes(len);
	}

	void notifyBodyError(Throwable error)
	{
		IOException converted;
		if (error == null)
		{
			converted = new IOException("request body error");
		}
		else if (error instanceof IOException ioException)
		{
			converted = ioException;
		}
		else
		{
			converted = new IOException("request body error", error);
		}
		terminalError.compareAndSet(null, converted);
		scheduleError();
	}

	private void checkBlockingOnly()
	{
		if (nonBlocking)
		{
			throw new IllegalStateException("operation is illegal in non-blocking mode");
		}
	}

	private void checkReadPermission()
	{
		if (!nonBlocking || callbackActive || readyPermit)
		{
			return;
		}
		throw new IllegalStateException(
				"non-blocking read requires isReady() to return true");
	}

	private void scheduleDataAvailable(boolean initial)
	{
		if (closed || listener.get() == null)
		{
			return;
		}
		if (!initial && !waitingForData)
		{
			return;
		}
		if (!callbackScheduled.compareAndSet(false, true))
		{
			return;
		}
		callbackExecutor.execute(this::invokeDataAvailable);
	}

	private void invokeDataAvailable()
	{
		try
		{
			ReadListener current = listener.get();
			if (closed || current == null || terminalError.get() != null)
			{
				return;
			}
			waitingForData = false;
			callbackActive = true;
			readyPermit = true;
			synchronized (listenerLock)
			{
				current = listener.get();
				if (!closed && current != null && terminalError.get() == null)
				{
					current.onDataAvailable();
				}
			}
		}
		catch (Throwable error)
		{
			notifyBodyError(error);
		}
		finally
		{
			callbackActive = false;
			readyPermit = false;
			callbackScheduled.set(false);
			if (terminalError.get() == null && source.isFinished())
			{
				scheduleAllDataRead();
			}
			else if (terminalError.get() == null && waitingForData && source.isReady())
			{
				scheduleDataAvailable(false);
			}
		}
	}

	private void scheduleAllDataRead()
	{
		ReadListener current = listener.get();
		if (closed || current == null || terminalError.get() != null
				|| !allDataReadNotified.compareAndSet(false, true))
		{
			return;
		}
		callbackExecutor.execute(() ->
			{
				synchronized (listenerLock)
				{
					ReadListener currentListener = listener.get();
					if (closed || currentListener == null || terminalError.get() != null)
					{
						return;
					}
					try
					{
						currentListener.onAllDataRead();
					}
					catch (Throwable error)
					{
						notifyBodyError(error);
					}
				}
			});
	}

	private void scheduleError()
	{
		if (closed || listener.get() == null || terminalError.get() == null
				|| !errorNotified.compareAndSet(false, true))
		{
			return;
		}
		callbackExecutor.execute(() ->
			{
				synchronized (listenerLock)
				{
					ReadListener currentListener = listener.get();
					IOException error = terminalError.get();
					if (closed || currentListener == null || error == null)
					{
						return;
					}
					try
					{
						currentListener.onError(error);
					}
					catch (Throwable ignored)
					{
						/* Terminal error remains authoritative. */
					}
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
