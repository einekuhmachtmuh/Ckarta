package org.ckarta.servlet;

/**
 * Container-internal bridge from one Servlet async cycle to one native
 * connection registry entry.
 *
 * <p>The native handle values are opaque capabilities. This package-private
 * class is not exposed as application-facing Servlet API.</p>
 */
final class CkartaNativeAsyncBridge
		implements CkartaAsyncCycleBinding.NativeTerminalBridge
{
	private final long registryHandle;
	private final long connectionHandle;

	CkartaNativeAsyncBridge(long registryHandle, long connectionHandle)
	{
		if (registryHandle <= 0L)
		{
			throw new IllegalArgumentException("registryHandle must be positive");
		}
		if (connectionHandle <= 0L)
		{
			throw new IllegalArgumentException(
					"connectionHandle must be positive");
		}

		this.registryHandle = registryHandle;
		this.connectionHandle = connectionHandle;
	}

	@Override
	public int startCycle(
			long requestId,
			long ownerToken,
			long lifetimeToken,
			long cycleId)
	{
		return nativeStartAsyncCycle(
				registryHandle,
				connectionHandle,
				requestId,
				ownerToken,
				lifetimeToken,
				cycleId);
	}

	@Override
	public int tryTerminal(
			long requestId,
			long ownerToken,
			long lifetimeToken,
			long cycleId,
			CkartaAsyncContext.TerminalEvent event)
	{
		return nativeTryTerminal(
				registryHandle,
				connectionHandle,
				requestId,
				ownerToken,
				lifetimeToken,
				cycleId,
				event.ordinal());
	}

	private static native int nativeStartAsyncCycle(
			long registryHandle,
			long connectionHandle,
			long requestId,
			long ownerToken,
			long lifetimeToken,
			long cycleId);

	private static native int nativeTryTerminal(
			long registryHandle,
			long connectionHandle,
			long requestId,
			long ownerToken,
			long lifetimeToken,
			long cycleId,
			int event);
}
