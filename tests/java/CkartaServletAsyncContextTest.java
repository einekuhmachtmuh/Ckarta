package org.ckarta.servlet;

import java.lang.reflect.Proxy;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

import jakarta.servlet.AsyncListener;
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
		assert context.getTimeout() == -1L;

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
