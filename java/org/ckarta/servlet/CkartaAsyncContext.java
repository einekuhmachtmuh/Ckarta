package org.ckarta.servlet;

import java.util.Objects;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Internal Servlet async lifecycle core.
 *
 * <p>This class deliberately does not implement jakarta.servlet.AsyncContext yet.
 * It contains only the container-side lifecycle semantics that do not require
 * the Jakarta Servlet API artifact. A future Jakarta-facing adapter must map
 * the official API to this core without exposing native connection state.</p>
 */
public final class CkartaAsyncContext
{
	public enum TerminalEvent
	{
		COMPLETE,
		TIMEOUT,
		ERROR,
		CLIENT_DISCONNECT,
		SHUTDOWN
	}

	public enum State
	{
		ACTIVE,
		COMPLETING,
		COMPLETED,
		TIMING_OUT,
		ERRORED,
		RECYCLED
	}

	@FunctionalInterface
	public interface TerminalSink
	{
		void onTerminal(TerminalEvent event, Throwable error);
	}

	@FunctionalInterface
	public interface Listener
	{
		void onEvent(TerminalEvent event, Throwable error);
	}

	private final Executor executor;
	private final TerminalSink terminalSink;
	private final AtomicReference<State> state =
			new AtomicReference<>(State.ACTIVE);
	private final AtomicBoolean listenerFired = new AtomicBoolean(false);
	private final java.util.concurrent.CopyOnWriteArrayList<Listener> listeners =
			new java.util.concurrent.CopyOnWriteArrayList<>();

	public CkartaAsyncContext(Executor executor, TerminalSink terminalSink)
	{
		this.executor = Objects.requireNonNull(executor, "executor");
		this.terminalSink = Objects.requireNonNull(terminalSink, "terminalSink");
	}

	public State state()
	{
		return state.get();
	}

	public void addListener(Listener listener)
	{
		Objects.requireNonNull(listener, "listener");
		checkUsable();
		if (state.get() != State.ACTIVE)
		{
			throw new IllegalStateException("async context is no longer active");
		}
		listeners.add(listener);
	}

	public void start(Runnable runnable)
	{
		Objects.requireNonNull(runnable, "runnable");
		checkUsable();

		try
		{
			executor.execute(() ->
			{
				try
				{
					runnable.run();
				}
				catch (Throwable error)
				{
					terminate(TerminalEvent.ERROR, error);
				}
			});
		}
		catch (RuntimeException error)
		{
			terminate(TerminalEvent.ERROR, error);
			throw error;
		}
	}

	public void complete()
	{
		terminate(TerminalEvent.COMPLETE, null);
	}

	public void timeout()
	{
		terminate(TerminalEvent.TIMEOUT, null);
	}

	public void error(Throwable error)
	{
		terminate(TerminalEvent.ERROR, Objects.requireNonNull(error, "error"));
	}

	public void clientDisconnect()
	{
		terminate(TerminalEvent.CLIENT_DISCONNECT, null);
	}

	public void shutdown()
	{
		terminate(TerminalEvent.SHUTDOWN, null);
	}

	public void recycle()
	{
		if (!state.compareAndSet(State.COMPLETED, State.RECYCLED))
		{
			throw new IllegalStateException(
					"async context can only be recycled after completion");
		}
	}

	private void terminate(TerminalEvent event, Throwable error)
	{
		State terminalState = switch (event)
		{
			case COMPLETE -> State.COMPLETING;
			case TIMEOUT -> State.TIMING_OUT;
			default -> State.ERRORED;
		};

		if (!state.compareAndSet(State.ACTIVE, terminalState))
		{
			throw new IllegalStateException("async operation is no longer active");
		}

		/*
		 * The state transition is the single linearization point for terminal
		 * ownership. The sink sees exactly one terminal event per async cycle.
		 */
		terminalSink.onTerminal(event, error);
		fireListenerOnce(event, error);

		state.set(State.COMPLETED);
	}

	private void fireListenerOnce(TerminalEvent event, Throwable error)
	{
		if (!listenerFired.compareAndSet(false, true))
		{
			return;
		}

		for (Listener listener : listeners)
		{
			try
			{
				listener.onEvent(event, error);
			}
			catch (Throwable ignored)
			{
				/*
				 * Listener failure is diagnostic at this semantic-core level.
				 * The terminal outcome has already been linearized and must
				 * not be changed by a listener failure.
				 */
			}
		}
	}

	private void checkUsable()
	{
		if (state.get() == State.RECYCLED)
		{
			throw new IllegalStateException("async context has been recycled");
		}
	}
}
