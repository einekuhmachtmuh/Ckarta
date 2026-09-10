package org.ckarta.servlet;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.lang.reflect.Proxy;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

import jakarta.servlet.AsyncListener;
import jakarta.servlet.ReadListener;
import jakarta.servlet.ServletRequest;
import jakarta.servlet.ServletResponse;

public final class CkartaServletAsyncContextTest
{
	private CkartaServletAsyncContextTest()
	{
	}

	private static Object defaultValue(Class<?> type)
	{
		if (!type.isPrimitive())
		{
			return null;
		}
		if (type == boolean.class)
		{
			return false;
		}
		if (type == byte.class)
		{
			return (byte) 0;
		}
		if (type == short.class)
		{
			return (short) 0;
		}
		if (type == int.class)
		{
			return 0;
		}
		if (type == long.class)
		{
			return 0L;
		}
		if (type == float.class)
		{
			return 0.0f;
		}
		if (type == double.class)
		{
			return 0.0d;
		}
		if (type == char.class)
		{
			return '\0';
		}
		throw new AssertionError("unsupported primitive: " + type);
	}

	private static <T> T mock(Class<T> type)
	{
		return type.cast(Proxy.newProxyInstance(
				type.getClassLoader(),
				new Class<?>[] { type },
				(proxy, method, args) -> defaultValue(method.getReturnType())));
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

	private static final class BodySource implements CkartaServletInputStream.BodySource
	{
		private final byte[] data;
		private int position;
		private boolean available;
		private boolean eof;
		private Runnable readyListener;
		private Runnable errorListener;
		private IOException error;

		BodySource(byte[] data)
		{
			this.data = data;
			available = data.length > 0;
			eof = data.length == 0;
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

	public static void main(String[] args) throws Exception
	{
		Executor executor = Runnable::run;
		ServletRequest request = mock(ServletRequest.class);
		ServletResponse response = mock(ServletResponse.class);
		AtomicInteger completeCount = new AtomicInteger();
		AtomicReference<Throwable> error = new AtomicReference<>();

		CkartaAsyncContext core = new CkartaAsyncContext(
				executor,
				(event, throwable) ->
				{
					completeCount.incrementAndGet();
					error.set(throwable);
				});
		CkartaServletAsyncContext context = new CkartaServletAsyncContext(
				core, request, response, true);

		assert context.getRequest() == request;
		assert context.getResponse() == response;
		assert context.hasOriginalRequestAndResponse();
		assert context.getTimeout() == 30_000L;

		context.setTimeout(0L);
		assert context.getTimeout() == 0L;

		AtomicInteger listenerCount = new AtomicInteger();
		AsyncListener listener = new AsyncListener()
		{
			@Override
			public void onComplete(jakarta.servlet.AsyncEvent event)
			{
				listenerCount.incrementAndGet();
			}

			@Override
			public void onTimeout(jakarta.servlet.AsyncEvent event)
			{
			}

			@Override
			public void onError(jakarta.servlet.AsyncEvent event)
			{
			}

			@Override
			public void onStartAsync(jakarta.servlet.AsyncEvent event)
			{
			}
		};

		context.addListener(listener);
		context.complete();

		assert completeCount.get() == 1;
		assert listenerCount.get() == 1;
		assert error.get() == null;

		boolean timeoutRejected = false;
		try
		{
			context.setTimeout(1000L);
		}
		catch (IllegalStateException expected)
		{
			timeoutRejected = true;
		}
		assert timeoutRejected;

		boolean requestRejected = false;
		try
		{
			context.getRequest();
		}
		catch (IllegalStateException expected)
		{
			requestRejected = true;
		}
		assert requestRejected;

		boolean dispatchRejected = false;
		try
		{
			context.dispatch();
		}
		catch (UnsupportedOperationException expected)
		{
			dispatchRejected = true;
		}
		assert dispatchRejected;

		BodySource source = new BodySource("abc".getBytes(StandardCharsets.US_ASCII));
		CkartaServletInputStream stream =
				new CkartaServletInputStream(source, executor, true);
		AtomicInteger dataCallbacks = new AtomicInteger();
		AtomicInteger allDataCallbacks = new AtomicInteger();
		AtomicReference<Throwable> streamError = new AtomicReference<>();
		stream.setReadListener(new ReadListener()
		{
			@Override
			public void onDataAvailable() throws IOException
			{
				dataCallbacks.incrementAndGet();
				ByteBuffer buffer = ByteBuffer.allocate(3);
				int initialPosition = buffer.position();
				int initialLimit = buffer.limit();
				assertEquals(3, stream.read(buffer), "non-blocking callback read length");
				assertEquals(initialPosition, buffer.position(), "ServletInputStream position");
				assertEquals(initialPosition + 3, buffer.limit(), "ServletInputStream limit");
				assertTrue(initialLimit == buffer.capacity(), "test buffer capacity");
			}

			@Override
			public void onAllDataRead()
			{
				allDataCallbacks.incrementAndGet();
			}

			@Override
			public void onError(Throwable throwable)
			{
				streamError.set(throwable);
			}
		});
		assertEquals(1, dataCallbacks.get(), "initial data callback");
		assertEquals(1, allDataCallbacks.get(), "all-data callback");
		assertTrue(streamError.get() == null, "unexpected stream error");

		BodySource waitingSource = new BodySource("xyz".getBytes(StandardCharsets.US_ASCII));
		waitingSource.available = false;
		waitingSource.eof = false;
		CkartaServletInputStream waiting =
				new CkartaServletInputStream(waitingSource, executor, true);
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
		assertTrue(!waiting.isReady(), "waiting stream must be non-ready");
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

		BodySource errorSource = new BodySource("e".getBytes(StandardCharsets.US_ASCII));
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

		System.out.println("CKARTA_SERVLET_ASYNC_API_OK");
	}

	public static final class AsyncListenerImpl implements AsyncListener
	{
		@Override
		public void onComplete(jakarta.servlet.AsyncEvent event)
		{
		}

		@Override
		public void onTimeout(jakarta.servlet.AsyncEvent event)
		{
		}

		@Override
		public void onError(jakarta.servlet.AsyncEvent event)
		{
		}

		@Override
		public void onStartAsync(jakarta.servlet.AsyncEvent event)
		{
		}
	}
}
