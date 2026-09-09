package org.ckarta.servlet;

import java.util.Objects;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Internal Servlet async lifecycle core.
 *
 * <p>This class deliberately does not implement jakarta.servlet.AsyncContext yet.
 * It contains only container-side lifecycle semantics that do not require the
 * Jakarta Servlet API artifact. A Jakarta-facing adapter maps the official API
 * to this core without exposing native connection state.</p>
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

	public enum TerminalDecision
	{
		CLAIMED,
		ALREADY_CLAIMED,
		LOST,
		ERROR
	}

	@FunctionalInterface
	public interface TerminalGate
	{
		TerminalDecision tryTerminal(TerminalEvent event);
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
	private final CkartaAsyncCycleBinding cycleBinding;
	private final TerminalGate terminalGate;
	private final AtomicReference<State> state =
			new AtomicReference<>(State.ACTIVE);
	private final AtomicBoolean listenerFired = new AtomicBoolean(false);
	private final CopyOnWriteArrayList<Listener> listeners =
			new CopyOnWriteArrayList<>();

	public CkartaAsyncContext(Executor executor, TerminalSink terminalSink)
	{
		this(executor, terminalSink, null);
	}

	public CkartaAsyncContext(
			Executor executor,
			TerminalSink terminalSink,
			CkartaAsyncCycleBinding cycleBinding)
	{
		this.executor = Objects.requireNonNull(executor, "executor");
		this.terminalSink = Objects.requireNonNull(terminalSink, "terminalSink");
		this.cycleBinding = cycleBinding;
		this.terminalGate = cycleBinding == null
				? null : cycleBinding.terminalGate();
	}

	CkartaAsyncCycleBinding cycleBinding()
	{
		return cycleBinding;
	}

	public State state()
	{
		return state.get();
	}

	public void addListener(Listener listener)
	{
		Objects.requireNonNull(listener, "listener");
		checkActive();

		if (state.get() != State.ACTIVE)
		{
			throw new IllegalStateException("async context is no longer active");
		}

		listeners.add(listener);
	}

	public void start(Runnable runnable)
	{
		Objects.requireNonNull(runnable, "runnable");
		checkActive();

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
					tryTerminate(TerminalEvent.ERROR, error, false);
				}
			});
		}
		catch (RuntimeException error)
		{
			if (tryTerminate(TerminalEvent.ERROR, error, true))
			{
				throw error;
			}
			throw new IllegalStateException(
					"async operation is no longer active", error);
		}
	}

	public void complete()
	{
		if (!tryTerminate(TerminalEvent.COMPLETE, null, true))
		{
			throw new IllegalStateException("async operation is no longer active");
		}
	}

	public void timeout()
	{
		tryTerminate(TerminalEvent.TIMEOUT, null, false);
	}

	public void error(Throwable error)
	{
		Objects.requireNonNull(error, "error");
		tryTerminate(TerminalEvent.ERROR, error, false);
	}

	public void clientDisconnect()
	{
		tryTerminate(TerminalEvent.CLIENT_DISCONNECT, null, false);
	}

	public void shutdown()
	{
		tryTerminate(TerminalEvent.SHUTDOWN, null, false);
	}

	public void recycle()
	{
		if (!state.compareAndSet(State.COMPLETED, State.RECYCLED))
		{
			throw new IllegalStateException(
					"async context can only be recycled after completion");
		}
	}

	private boolean tryTerminate(
			TerminalEvent event, Throwable error, boolean failOnRace)
	{
		if (terminalGate != null)
		{
			TerminalDecision decision = terminalGate.tryTerminal(event);
			if (decision == TerminalDecision.ERROR)
			{
				if (failOnRace)
				{
					throw new IllegalStateException(
							"native async terminal arbitration failed");
				}
				return false;
			}
			if (decision == TerminalDecision.LOST)
			{
				return false;
			}
		}

		State terminalState = switch (event)
		{
			case COMPLETE -> State.COMPLETING;
			case TIMEOUT -> State.TIMING_OUT;
			default -> State.ERRORED;
		};

		if (!state.compareAndSet(State.ACTIVE, terminalState))
		{
			return false;
		}

		RuntimeException sinkFailure = null;
		try
		{
			terminalSink.onTerminal(event, error);
		}
		catch (RuntimeException failure)
		{
			sinkFailure = failure;
		}
		finally
		{
			fireListenerOnce(event, error);
			state.set(State.COMPLETED);
			if (cycleBinding != null)
			{
				cycleBinding.invalidate();
			}
		}

		if (sinkFailure != null && failOnRace)
		{
			throw sinkFailure;
		}

		return true;
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
				 * Listener failure must not alter the already-linearized
				 * terminal outcome at this semantic-core layer.
				 */
			}
		}
	}

	private void checkActive()
	{
		if (state.get() != State.ACTIVE)
		{
			throw new IllegalStateException(
					"async context is no longer active");
		}
	}
}
