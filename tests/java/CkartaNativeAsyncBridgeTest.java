package org.ckarta.servlet;

import java.lang.reflect.Proxy;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicInteger;

import jakarta.servlet.AsyncContext;
import jakarta.servlet.ServletRequest;
import jakarta.servlet.ServletResponse;

public final class CkartaNativeAsyncBridgeTest
{
	private CkartaNativeAsyncBridgeTest()
	{
	}

	private static Object primitiveDefault(Class<?> type)
	{
		if (type == boolean.class) return false;
		if (type == byte.class) return (byte) 0;
		if (type == short.class) return (short) 0;
		if (type == int.class) return 0;
		if (type == long.class) return 0L;
		if (type == float.class) return 0.0f;
		if (type == double.class) return 0.0d;
		if (type == char.class) return '\0';
		return null;
	}

	private static ServletRequest requestMock()
	{
		return (ServletRequest) Proxy.newProxyInstance(
				ServletRequest.class.getClassLoader(),
				new Class<?>[] { ServletRequest.class },
				(proxy, method, args) ->
				{
					if (method.getName().equals("isAsyncSupported"))
					{
						return true;
					}
					return method.getReturnType().isPrimitive()
							? primitiveDefault(method.getReturnType()) : null;
				});
	}

	private static ServletResponse responseMock()
	{
		return (ServletResponse) Proxy.newProxyInstance(
				ServletResponse.class.getClassLoader(),
				new Class<?>[] { ServletResponse.class },
				(proxy, method, args) -> method.getReturnType().isPrimitive()
						? primitiveDefault(method.getReturnType()) : null);
	}

	public static void run(
			long registryHandle,
			long firstConnectionHandle,
			long secondConnectionHandle)
	{
		Executor executor = Runnable::run;

		ServletRequest firstRequest = requestMock();
		ServletResponse firstResponse = responseMock();
		AtomicInteger firstTerminalCount = new AtomicInteger();
		CkartaNativeAsyncBridge firstBridge =
				new CkartaNativeAsyncBridge(
						registryHandle, firstConnectionHandle);
		CkartaServletRequestAdapter firstAdapter =
				new CkartaServletRequestAdapter(
						firstRequest,
						firstResponse,
						executor,
						(event, error) -> firstTerminalCount.incrementAndGet(),
						11L,
						22L,
						33L,
						firstBridge);

		AsyncContext firstContext = firstAdapter.startAsync();
		CkartaServletAsyncContext firstServletContext =
				(CkartaServletAsyncContext) firstContext;
		CkartaAsyncCycleBinding firstBinding =
				firstServletContext.cycleBinding();

		assert firstBinding.isActive();
		assert firstBinding.matches(
				11L, 22L, 33L, firstBinding.cycleId());
		assert firstAdapter.isAsyncStarted();

		firstContext.complete();

		assert firstTerminalCount.get() == 1;
		assert !firstAdapter.isAsyncStarted();
		assert !firstBinding.isActive();

		ServletRequest secondRequest = requestMock();
		ServletResponse secondResponse = responseMock();
		AtomicInteger secondTerminalCount = new AtomicInteger();
		CkartaNativeAsyncBridge secondBridge =
				new CkartaNativeAsyncBridge(
						registryHandle, secondConnectionHandle);
		CkartaServletRequestAdapter secondAdapter =
				new CkartaServletRequestAdapter(
						secondRequest,
						secondResponse,
						executor,
						(event, error) -> secondTerminalCount.incrementAndGet(),
						44L,
						55L,
						66L,
						secondBridge);

		AsyncContext secondContext = secondAdapter.startAsync();
		CkartaServletAsyncContext secondServletContext =
				(CkartaServletAsyncContext) secondContext;
		CkartaAsyncCycleBinding secondBinding =
				secondServletContext.cycleBinding();

		assert secondAdapter.isAsyncStarted();
		assert secondBridge.tryTerminal(
				44L, 55L, 66L, secondBinding.cycleId(),
				CkartaAsyncContext.TerminalEvent.CLIENT_DISCONNECT) == 0;

		boolean differentNativeWinnerRejected = false;
		try
		{
			secondContext.complete();
		}
		catch (IllegalStateException expected)
		{
			differentNativeWinnerRejected = true;
		}
		assert differentNativeWinnerRejected;
		assert secondAdapter.isAsyncStarted();
		assert secondBinding.isActive();
		assert secondTerminalCount.get() == 0;

		secondServletContext.notifyClientDisconnect();

		assert secondTerminalCount.get() == 1;
		assert !secondAdapter.isAsyncStarted();
		assert !secondBinding.isActive();

		System.out.println("CKARTA_NATIVE_ASYNC_BRIDGE_OK");
	}
}
