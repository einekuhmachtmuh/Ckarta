package org.ckarta.servlet;

import java.util.Objects;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

import jakarta.servlet.AsyncContext;
import jakarta.servlet.ServletRequest;
import jakarta.servlet.ServletRequestWrapper;
import jakarta.servlet.ServletResponse;

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
	private final AtomicReference<CkartaServletAsyncContext> asyncContext =
			new AtomicReference<>();
	private final AtomicBoolean asyncStarted = new AtomicBoolean(false);

	public CkartaServletRequestAdapter(
			ServletRequest request,
			ServletResponse originalResponse,
			Executor asyncExecutor,
			CkartaAsyncContext.TerminalSink terminalSink)
	{
		super(Objects.requireNonNull(request, "request"));
		this.originalResponse =
				Objects.requireNonNull(originalResponse, "originalResponse");
		this.asyncExecutor =
				Objects.requireNonNull(asyncExecutor, "asyncExecutor");
		this.terminalSink =
				Objects.requireNonNull(terminalSink, "terminalSink");
	}

	@Override
	public AsyncContext startAsync()
	{
		return startAsync(this, originalResponse, true);
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

		if (!asyncStarted.compareAndSet(false, true))
		{
			throw new IllegalStateException(
					"request is already in asynchronous mode");
		}

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
						if (event != CkartaAsyncContext.TerminalEvent.ERROR)
						{
							asyncStarted.set(false);
						}
					}
				});

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
