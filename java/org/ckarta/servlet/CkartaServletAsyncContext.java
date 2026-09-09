package org.ckarta.servlet;

import java.io.IOException;
import java.lang.reflect.InvocationTargetException;
import java.util.Objects;
import java.util.concurrent.atomic.AtomicLong;

import jakarta.servlet.AsyncContext;
import jakarta.servlet.AsyncEvent;
import jakarta.servlet.AsyncListener;
import jakarta.servlet.ServletException;
import jakarta.servlet.ServletRequest;
import jakarta.servlet.ServletResponse;

/**
 * Thin Jakarta Servlet 6.1 adapter over the Ckarta async semantic core.
 *
 * <p>This class establishes the application-facing API boundary. It does not
 * expose native connection state, native pointers, or queue implementation
 * details. Unsupported operations are explicit until the corresponding
 * Servlet container routing and dispatch infrastructure exists.</p>
 */
public final class CkartaServletAsyncContext implements AsyncContext
{
	private static final long DEFAULT_TIMEOUT_MILLIS = 30_000L;
	private final CkartaAsyncContext core;
	private final ServletRequest request;
	private final ServletResponse response;
	private final boolean originalRequestAndResponse;
	private final AtomicLong timeout = new AtomicLong(DEFAULT_TIMEOUT_MILLIS);
	private final CkartaAsyncCycleBinding cycleBinding;

	public CkartaServletAsyncContext(
			CkartaAsyncContext core,
			ServletRequest request,
			ServletResponse response,
			boolean originalRequestAndResponse)
	{
		this.core = Objects.requireNonNull(core, "core");
		this.request = Objects.requireNonNull(request, "request");
		this.response = Objects.requireNonNull(response, "response");
		this.originalRequestAndResponse = originalRequestAndResponse;
		this.cycleBinding = core.cycleBinding();
	}

	@Override
	public void complete()
	{
		core.complete();
	}

	@Override
	public void start(Runnable runnable)
	{
		core.start(Objects.requireNonNull(runnable, "runnable"));
	}

	@Override
	public void dispatch()
	{
		throw new UnsupportedOperationException("async dispatch is not implemented");
	}

	@Override
	public void dispatch(String path)
	{
		Objects.requireNonNull(path, "path");
		throw new UnsupportedOperationException("async dispatch is not implemented");
	}

	@Override
	public void dispatch(jakarta.servlet.ServletContext context, String path)
	{
		Objects.requireNonNull(context, "context");
		Objects.requireNonNull(path, "path");
		throw new UnsupportedOperationException("async dispatch is not implemented");
	}

	@Override
	public void addListener(AsyncListener listener)
	{
		Objects.requireNonNull(listener, "listener");
		core.addListener((event, error) -> fireListener(listener, event, error));
	}

	@Override
	public void addListener(
			AsyncListener listener,
			ServletRequest suppliedRequest,
			ServletResponse suppliedResponse)
	{
		Objects.requireNonNull(listener, "listener");
		Objects.requireNonNull(suppliedRequest, "suppliedRequest");
		Objects.requireNonNull(suppliedResponse, "suppliedResponse");
		core.addListener((event, error) ->
				fireListener(listener, event, error, suppliedRequest, suppliedResponse));
	}

	@Override
	public <T extends AsyncListener> T createListener(Class<T> clazz)
			throws ServletException
	{
		Objects.requireNonNull(clazz, "clazz");
		checkActive();

		try
		{
			var constructor = clazz.getDeclaredConstructor();
			if (!constructor.canAccess(null))
			{
				constructor.setAccessible(true);
			}
			return constructor.newInstance();
		}
		catch (NoSuchMethodException
				| InstantiationException
				| IllegalAccessException
				| InvocationTargetException exception)
		{
			throw new ServletException(exception);
		}
	}

	@Override
	public ServletRequest getRequest()
	{
		checkActive();
		return request;
	}

	@Override
	public ServletResponse getResponse()
	{
		checkActive();
		return response;
	}

	@Override
	public long getTimeout()
	{
		checkActive();
		return timeout.get();
	}

	@Override
	public void setTimeout(long timeout)
	{
		checkActive();
		this.timeout.set(timeout);
	}

	@Override
	public boolean hasOriginalRequestAndResponse()
	{
		checkActive();
		return originalRequestAndResponse;
	}

	CkartaAsyncCycleBinding cycleBinding()
	{
		return cycleBinding;
	}

	void notifyClientDisconnect()
	{
		core.clientDisconnect();
	}

	private void checkActive()
	{
		if (core.state() != CkartaAsyncContext.State.ACTIVE)
		{
			throw new IllegalStateException(
					"async context is no longer active");
		}
	}

	private void fireListener(
			AsyncListener listener,
			CkartaAsyncContext.TerminalEvent event,
			Throwable error)
	{
		fireListener(listener, event, error, request, response);
	}

	private void fireListener(
			AsyncListener listener,
			CkartaAsyncContext.TerminalEvent event,
			Throwable error,
			ServletRequest suppliedRequest,
			ServletResponse suppliedResponse)
	{
		AsyncEvent asyncEvent = new AsyncEvent(
				this, suppliedRequest, suppliedResponse, error);

		try
		{
			switch (event)
			{
				case COMPLETE -> listener.onComplete(asyncEvent);
				case CLIENT_DISCONNECT, SHUTDOWN ->
					{ }

				case TIMEOUT ->
						listener.onTimeout(asyncEvent);
				case ERROR ->
						listener.onError(asyncEvent);
			}
		}
		catch (IOException exception)
		{
			/*
			 * Listener I/O failure must not replace the already-linearized
			 * async terminal outcome. A future container logging bridge should
			 * record this exception through the diagnostic channel.
			 */
		}
	}
}
