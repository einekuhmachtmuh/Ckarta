package org.ckarta.servlet;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

public final class CkartaAsyncContextTest
{
	private CkartaAsyncContextTest()
	{
	}

	public static void main(String[] args) throws Exception
	{
		ExecutorService executor = Executors.newSingleThreadExecutor();
		try
		{
			AtomicInteger terminalCount = new AtomicInteger();
			AtomicInteger listenerCount = new AtomicInteger();
			AtomicReference<CkartaAsyncContext.TerminalEvent> event =
					new AtomicReference<>();

			CkartaAsyncContext context = new CkartaAsyncContext(
					executor,
					(terminalEvent, error) ->
					{
						event.set(terminalEvent);
						terminalCount.incrementAndGet();
					});
			context.addListener((terminalEvent, error) ->
					listenerCount.incrementAndGet());

			context.complete();

			assert context.state() == CkartaAsyncContext.State.COMPLETED;
			assert event.get() == CkartaAsyncContext.TerminalEvent.COMPLETE;
			assert terminalCount.get() == 1;
			assert listenerCount.get() == 1;

			boolean rejected = false;
			try
			{
				context.complete();
			}
			catch (IllegalStateException expected)
			{
				rejected = true;
			}
			assert rejected;

			context.timeout();
			assert terminalCount.get() == 1;
			assert listenerCount.get() == 1;
		}
		finally
		{
			executor.shutdownNow();
		}

		ExecutorService raceExecutor = Executors.newFixedThreadPool(2);
		try
		{
			AtomicInteger terminalCount = new AtomicInteger();
			AtomicInteger listenerCount = new AtomicInteger();
			AtomicReference<CkartaAsyncContext.TerminalEvent> event =
					new AtomicReference<>();

			CkartaAsyncContext context = new CkartaAsyncContext(
					raceExecutor,
					(terminalEvent, error) ->
					{
						event.set(terminalEvent);
						terminalCount.incrementAndGet();
					});
			context.addListener((terminalEvent, error) ->
					listenerCount.incrementAndGet());

			Thread completeThread = new Thread(() ->
			{
				try
				{
					context.complete();
				}
				catch (IllegalStateException ignored)
				{
				}
			});
			Thread timeoutThread = new Thread(context::timeout);
			completeThread.start();
			timeoutThread.start();
			completeThread.join();
			timeoutThread.join();

			assert terminalCount.get() == 1;
			assert listenerCount.get() == 1;
			assert context.state() == CkartaAsyncContext.State.COMPLETED;
			assert event.get() == CkartaAsyncContext.TerminalEvent.COMPLETE
					|| event.get() == CkartaAsyncContext.TerminalEvent.TIMEOUT;
		}
		finally
		{
			raceExecutor.shutdownNow();
		}

		ExecutorService asyncExecutor = Executors.newSingleThreadExecutor();
		try
		{
			AtomicInteger ran = new AtomicInteger();
			CkartaAsyncContext context = new CkartaAsyncContext(
					asyncExecutor,
					(terminalEvent, error) ->
					{
						if (terminalEvent == CkartaAsyncContext.TerminalEvent.ERROR)
						{
							ran.incrementAndGet();
						}
					});

			context.start(() ->
			{
				throw new IllegalStateException("async task failure");
			});

			for (int i = 0; i < 100 && context.state()
					!= CkartaAsyncContext.State.COMPLETED; i++)
			{
				Thread.sleep(5L);
			}

			assert context.state() == CkartaAsyncContext.State.COMPLETED;
			assert ran.get() == 1;
		}
		finally
		{
			asyncExecutor.shutdownNow();
		}
	}
}
