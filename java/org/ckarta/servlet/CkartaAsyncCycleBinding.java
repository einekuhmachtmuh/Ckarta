package org.ckarta.servlet;

import java.util.Objects;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Opaque identity binding for one Servlet asynchronous cycle.
 *
 * <p>The binding carries only correlation identity. It does not expose or own
 * native memory. A future native bridge may use this identity to validate the
 * native connection owner and lifetime without exposing a native pointer to
 * Servlet application code.</p>
 */
public final class CkartaAsyncCycleBinding
{
	private static final long MAX_CYCLE_ID = 0x0000FFFFFFFFFFFFL;
	private static final AtomicLong NEXT_CYCLE_ID = new AtomicLong(1L);

	private final long requestId;
	private final long ownerToken;
	private final long lifetimeToken;
	private final long cycleId;
	private final AtomicBoolean active = new AtomicBoolean(true);

	public CkartaAsyncCycleBinding(
			long requestId,
			long ownerToken,
			long lifetimeToken)
	{
		if (requestId <= 0L)
		{
			throw new IllegalArgumentException("requestId must be positive");
		}
		if (ownerToken < 0L || lifetimeToken < 0L)
		{
			throw new IllegalArgumentException("owner/lifetime token must be non-negative");
		}

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

		this.requestId = requestId;
		this.ownerToken = ownerToken;
		this.lifetimeToken = lifetimeToken;
		this.cycleId = generatedCycleId;
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

	void requireActive()
	{
		if (!active.get())
		{
			throw new IllegalStateException("async cycle binding is inactive");
		}
	}
}
