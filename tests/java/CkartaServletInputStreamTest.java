package org.ckarta.servlet;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

import jakarta.servlet.ReadListener;

public final class CkartaServletInputStreamTest
{
	private CkartaServletInputStreamTest()
	{
	}

	private static final class Source implements CkartaServletInputStream.BodySource
	{
		private final byte[] data;
		private int position;
		private boolean available;
		private boolean eof;
		private Runnable readyListener;
		private Runnable errorListener;
		private IOException error;

		Source(byte[] data)
		{
			this.data = data;
			this.available = data.length > 0;
			this.eof = data.length == 0;
		}

		@Override
		public int read(ByteBuffer destination, boolean blocking) throws IOException
		{
			if (error != null)
			{
				throw error;
			}
			if (!available)
			{
				return eof ? EOF : WOULD_BLOCK;
			}

			int count = Math.min(destination.remaining(), data.length - position);
			destination.put(data, position, count);
			position += count;
			if (position == data.length)
			{
				available = false;
				eof = true;
			}
			return DATA;
		}

		@Override
		public boolean isReady()
		{
			return available || eof;
		}

		@Override
		public boolean isFinished()
		{
			return eof && !available;
		}

		@Override
		public void setReadInterest(Runnable onReady)
		{
			readyListener = onReady;
		}

		@Override
		public void setErrorInterest(Runnable onError)
		{
			errorListener = onError;
		}

		@Override
		public void close()
		{
		}

		void makeAvailable()
		{
			available = true;
			if (readyListener != null)
			{
				readyListener.run();
			}
		}

		void fail(IOException failure)
		{
			error = failure;
			if (errorListener != null)
			{
				errorListener.run();
			}
		}
	}

	private static void assertTrue(boolean value, String message)
	{
		if (!value)
		{
			throw new AssertionError(message);
		}
	}

	private static void assertEquals(int expected, int actual, String message)
	{
		if (expected != actual)
		{
			throw new AssertionError(message + ": expected=" + expected + " actual=" + actual);
		}
	}

	public static void main(String[] args) throws Exception
	{
		Executor executor = Runnable::run;
		Source source = new Source("hello".getBytes(StandardCharsets.US_ASCII));
		CkartaServletInputStream stream =
				new CkartaServletInputStream(source, executor, true);

		assertTrue(stream.isReady(), "blocking mode must be ready");
		byte[] hello = new byte[5];
		assertEquals(5, stream.read(hello, 0, hello.length), "blocking read length");
		assertTrue(stream.isFinished(), "stream must be finished after full read");
		assertEquals(-1, stream.read(), "EOF read");

		Source nonBlockingSource = new Source("abc".getBytes(StandardCharsets.US_ASCII));
		CkartaServletInputStream nonBlocking =
				new CkartaServletInputStream(nonBlockingSource, executor, true);
		AtomicInteger dataCallbacks = new AtomicInteger();
		AtomicInteger allDataCallbacks = new AtomicInteger();
		AtomicReference<Throwable> callbackError = new AtomicReference<>();
		nonBlocking.setReadListener(new ReadListener()
		{
			@Override
			public void onDataAvailable() throws IOException
			{
				dataCallbacks.incrementAndGet();
				ByteBuffer buffer = ByteBuffer.allocate(3);
				assertEquals(3, nonBlocking.read(buffer), "non-blocking callback read length");
			}

			@Override
			public void onAllDataRead()
			{
				allDataCallbacks.incrementAndGet();
			}

			@Override
			public void onError(Throwable throwable)
			{
				callbackError.set(throwable);
			}
		});
		assertEquals(1, dataCallbacks.get(), "initial callback count");
		assertTrue(nonBlocking.isFinished(), "non-blocking stream finished");
		assertEquals(1, allDataCallbacks.get(), "all-data callback count");
		assertTrue(callbackError.get() == null, "unexpected callback error");

		Source waitingSource = new Source("xyz".getBytes(StandardCharsets.US_ASCII));
		CkartaServletInputStream waiting =
				new CkartaServletInputStream(waitingSource, executor, true);
		waitingSource.available = false;
		waitingSource.eof = false;
		AtomicInteger waitingCallbacks = new AtomicInteger();
		waiting.setReadListener(new ReadListener()
		{
			@Override
			public void onDataAvailable()
			{
				waitingCallbacks.incrementAndGet();
			}

			@Override
			public void onAllDataRead()
			{
			}

			@Override
			public void onError(Throwable throwable)
			{
				throw new AssertionError(throwable);
			}
		});
		assertTrue(!waiting.isReady(), "waiting stream must initially be non-ready");
		boolean illegalRead = false;
		try
		{
			waiting.read();
		}
		catch (IllegalStateException expected)
		{
			illegalRead = true;
		}
		assertTrue(illegalRead, "read without readiness must fail");
		waitingSource.makeAvailable();
		assertEquals(1, waitingCallbacks.get(), "readiness callback count");

		Source errorSource = new Source("e".getBytes(StandardCharsets.US_ASCII));
		CkartaServletInputStream errored =
				new CkartaServletInputStream(errorSource, executor, true);
		AtomicReference<Throwable> errorCallback = new AtomicReference<>();
		errored.setReadListener(new ReadListener()
		{
			@Override
			public void onDataAvailable()
			{
			}

			@Override
			public void onAllDataRead()
			{
			}

			@Override
			public void onError(Throwable throwable)
			{
				errorCallback.set(throwable);
			}
		});
		errorSource.fail(new IOException("boom"));
		assertTrue(errorCallback.get() != null, "error callback expected");

		System.out.println("CKARTA_SERVLET_INPUT_STREAM_OK");
	}
}
