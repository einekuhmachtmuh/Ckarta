package org.ckarta.servlet;

import java.util.Objects;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

import jakarta.servlet.AsyncContext;
import jakarta.servlet.ServletRequest;
import jakarta.servlet.ServletRequestWrapper;
import jakarta.servlet.ServletResponse;
import jakarta.servlet.ServletResponseWrapper;

/**
 * ServletRequest adapter that owns the application-facing async-cycle binding.
 *
 * <p>The wrapped request remains the source of all non-async Servlet semantics.
 * Native connection state is not exposed to the application.</p>
 */
public final class CkartaServletRequestAdapter extends ServletRequestWrapper
{
	private final ServletResponse originalResponse;
	private final Executor asyncExecutor;
	private final CkartaAsyncContext.TerminalSink terminalSink;
	private final CkartaAsyncCycleBinding.NativeTerminalBridge nativeBridge;
	private final long requestId;
	private final long ownerToken;
	private final long lifetimeToken;
	private final AtomicReference<CkartaAsyncCycleBinding> cycleBinding =
			new AtomicReference<>();
	private final AtomicReference<CkartaServletAsyncContext> asyncContext =
			new AtomicReference<>();
	private final AtomicBoolean asyncStarted = new AtomicBoolean(false);
	private final AtomicBoolean asyncCycleStarted = new AtomicBoolean(false);

	public CkartaServletRequestAdapter(
			ServletRequest request,
			ServletResponse originalResponse,
			Executor asyncExecutor,
			CkartaAsyncContext.TerminalSink terminalSink,
			long requestId,
			long ownerToken,
			long lifetimeToken)
	{
		this(request, originalResponse, asyncExecutor, terminalSink,
				requestId, ownerToken, lifetimeToken, null);
	}

	public CkartaServletRequestAdapter(
			ServletRequest request,
			ServletResponse originalResponse,
			Executor asyncExecutor,
			CkartaAsyncContext.TerminalSink terminalSink,
			long requestId,
			long ownerToken,
			long lifetimeToken,
			CkartaAsyncCycleBinding.NativeTerminalBridge nativeBridge)
	{
		super(Objects.requireNonNull(request, "request"));
		this.originalResponse =
				Objects.requireNonNull(originalResponse, "originalResponse");
		this.asyncExecutor =
				Objects.requireNonNull(asyncExecutor, "asyncExecutor");
		this.terminalSink =
				Objects.requireNonNull(terminalSink, "terminalSink");
		this.nativeBridge = nativeBridge;
		if (requestId <= 0L)
		{
			throw new IllegalArgumentException("requestId must be positive");
		}
		if (ownerToken < 0L || lifetimeToken < 0L)
		{
			throw new IllegalArgumentException(
					"owner/lifetime token must be non-negative");
		}
		this.requestId = requestId;
		this.ownerToken = ownerToken;
		this.lifetimeToken = lifetimeToken;
	}

	@Override
	public AsyncContext startAsync()
	{
		return startAsync(getRequest(), originalResponse,
				true);
	}

	@Override
	public AsyncContext startAsync(
			ServletRequest request,
			ServletResponse response)
	{
		Objects.requireNonNull(request, "request");
		Objects.requireNonNull(response, "response");
		return startAsync(request, response,
				request == this && response == originalResponse);
	}

	@Override
	public boolean isAsyncStarted()
	{
		return asyncStarted.get();
	}

	@Override
	public AsyncContext getAsyncContext()
	{
		CkartaServletAsyncContext context = asyncContext.get();
		if (!asyncStarted.get() || context == null)
		{
			throw new IllegalStateException(
					"request is not in asynchronous mode");
		}
		return context;
	}


	private boolean acceptsAsyncRequest(ServletRequest request)
	{
		if (request == getRequest())
		{
			return true;
		}
		return request instanceof ServletRequestWrapper wrapper
				&& wrapper.isWrapperFor(getRequest());
	}

	private boolean acceptsAsyncResponse(ServletResponse response)
	{
		if (response == originalResponse)
		{
			return true;
		}
		return response instanceof ServletResponseWrapper wrapper
				&& wrapper.isWrapperFor(originalResponse);
	}

	private AsyncContext startAsync(
			ServletRequest request,
			ServletResponse response,
			boolean originalRequestAndResponse)
	{
		if (!isAsyncSupported())
		{
			throw new IllegalStateException(
					"asynchronous processing is not supported");
		}

		if (!acceptsAsyncRequest(request) || !acceptsAsyncResponse(response))
		{
			throw new IllegalStateException(
					"request or response is not from this dispatch");
		}

		if (!asyncStarted.compareAndSet(false, true))
		{
			throw new IllegalStateException(
					"request is already in asynchronous mode");
		}

		if (!asyncCycleStarted.compareAndSet(false, true))
		{
			asyncStarted.set(false);
			throw new IllegalStateException(
					"request cannot start async processing again in this dispatch");
		}

		CkartaAsyncCycleBinding binding =
				new CkartaAsyncCycleBinding(
						requestId, ownerToken, lifetimeToken, nativeBridge);
		cycleBinding.set(binding);

		CkartaAsyncContext core = new CkartaAsyncContext(
				asyncExecutor,
				(event, error) ->
				{
					try
					{
						terminalSink.onTerminal(event, error);
					}
					finally
					{
						asyncStarted.set(false);
					}
				},
				binding);

		CkartaServletAsyncContext context =
				new CkartaServletAsyncContext(
						core,
						request,
						response,
						originalRequestAndResponse);
		asyncContext.set(context);
		return context;
	}
}
