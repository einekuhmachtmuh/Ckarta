package org.ckarta.servlet;

import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Opaque identity binding for one Servlet asynchronous cycle.
 *
 * <p>The public Servlet API never exposes the native connection handle. A
 * container-internal bridge may associate this binding with a native owner and
 * must arbitrate terminal events before the Java semantic core publishes its
 * local state transition.</p>
 */
public final class CkartaAsyncCycleBinding
{
	private static final long MAX_CYCLE_ID = 0x0000FFFFFFFFFFFFL;
	private static final AtomicLong NEXT_CYCLE_ID = new AtomicLong(1L);

	@FunctionalInterface
	public interface NativeTerminalBridge
	{
		int startCycle(
				long requestId,
				long ownerToken,
				long lifetimeToken,
				long cycleId);

		int tryTerminal(
				long requestId,
				long ownerToken,
				long lifetimeToken,
				long cycleId,
				CkartaAsyncContext.TerminalEvent event);
	}

	private final long requestId;
	private final long ownerToken;
	private final long lifetimeToken;
	private final long cycleId;
	private final NativeTerminalBridge nativeBridge;
	private final AtomicBoolean active = new AtomicBoolean(true);

	public CkartaAsyncCycleBinding(
			long requestId,
			long ownerToken,
			long lifetimeToken)
	{
		this(requestId, ownerToken, lifetimeToken, null);
	}

	CkartaAsyncCycleBinding(
			long requestId,
			long ownerToken,
			long lifetimeToken,
			NativeTerminalBridge nativeBridge)
	{
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
		this.nativeBridge = nativeBridge;

		long generatedCycleId;
		for (;;)
		{
			long current = NEXT_CYCLE_ID.get();
			if (current <= 0L || current > MAX_CYCLE_ID)
			{
				throw new IllegalStateException("async cycle id exhausted");
			}
			if (NEXT_CYCLE_ID.compareAndSet(current, current + 1L))
			{
				generatedCycleId = current;
				break;
			}
		}
		this.cycleId = generatedCycleId;

		if (nativeBridge != null
				&& nativeBridge.startCycle(
						requestId, ownerToken, lifetimeToken, cycleId) != 0)
		{
			active.set(false);
			throw new IllegalStateException(
					"native async cycle registration failed");
		}
	}

	public long requestId()
	{
		return requestId;
	}

	public long ownerToken()
	{
		return ownerToken;
	}

	public long lifetimeToken()
	{
		return lifetimeToken;
	}

	public long cycleId()
	{
		return cycleId;
	}

	public boolean isActive()
	{
		return active.get();
	}

	public boolean matches(
			long requestId,
			long ownerToken,
			long lifetimeToken,
			long cycleId)
	{
		return active.get()
				&& this.requestId == requestId
				&& this.ownerToken == ownerToken
				&& this.lifetimeToken == lifetimeToken
				&& this.cycleId == cycleId;
	}

	public void invalidate()
	{
		active.set(false);
	}

	CkartaAsyncContext.TerminalGate terminalGate()
	{
		if (nativeBridge == null)
		{
			return null;
		}

		return event ->
		{
			if (!active.get())
			{
				return CkartaAsyncContext.TerminalDecision.LOST;
			}

			int result = nativeBridge.tryTerminal(
					requestId, ownerToken, lifetimeToken, cycleId, event);
			switch (result)
			{
			case 0:
				return CkartaAsyncContext.TerminalDecision.CLAIMED;
			case 1:
				return CkartaAsyncContext.TerminalDecision.ALREADY_CLAIMED;
			case 2:
				return CkartaAsyncContext.TerminalDecision.LOST;
			default:
				return CkartaAsyncContext.TerminalDecision.ERROR;
			}
		};
	}
}
