#include "ck_connection.h"

static int ck_connection_valid_event(ck_connection_terminal_event_t event)
{
	return event >= CK_CONNECTION_TERMINAL_COMPLETE
			&& event <= CK_CONNECTION_TERMINAL_SHUTDOWN;
}

int ck_connection_init(ck_connection_t *connection,
		uint64_t connection_id,
		uint64_t request_id,
		uint64_t owner_token,
		uint64_t lifetime_token)
{
	if (connection == NULL || connection_id == 0 || request_id == 0)
	{
		return -1;
	}

	connection->connection_id = connection_id;
	connection->request_id = request_id;
	connection->owner_token = owner_token;
	connection->lifetime_token = lifetime_token;
	atomic_init(&connection->state, CK_CONNECTION_OPEN);
	atomic_init(&connection->terminal_event, -1);
	return 0;
}

int ck_connection_start_async(ck_connection_t *connection)
{
	uint32_t expected = CK_CONNECTION_OPEN;

	if (connection == NULL)
	{
		return -1;
	}

	return atomic_compare_exchange_strong_explicit(
			&connection->state, &expected, CK_CONNECTION_ASYNC_WAIT,
			memory_order_acq_rel, memory_order_acquire) ? 0 : 1;
}

int ck_connection_try_terminal(ck_connection_t *connection,
		ck_connection_terminal_event_t event)
{
	uint32_t current;

	if (connection == NULL || !ck_connection_valid_event(event))
	{
		return -1;
	}

	current = atomic_load_explicit(&connection->state, memory_order_acquire);
	for (;;)
	{
		if (current == CK_CONNECTION_CLOSING || current == CK_CONNECTION_CLOSED)
		{
			return 1;
		}

		if (current != CK_CONNECTION_OPEN && current != CK_CONNECTION_ASYNC_WAIT)
		{
			return -1;
		}

		if (atomic_compare_exchange_weak_explicit(
				&connection->state, &current, CK_CONNECTION_CLOSING,
				memory_order_acq_rel, memory_order_acquire))
		{
			atomic_store_explicit(&connection->terminal_event, event,
					memory_order_release);
			return 0;
		}
	}
}

int ck_connection_close(ck_connection_t *connection)
{
	uint32_t expected = CK_CONNECTION_CLOSING;

	if (connection == NULL)
	{
		return -1;
	}

	return atomic_compare_exchange_strong_explicit(
			&connection->state, &expected, CK_CONNECTION_CLOSED,
			memory_order_acq_rel, memory_order_acquire) ? 0 : 1;
}

int ck_connection_validate(const ck_connection_t *connection,
		uint64_t request_id,
		uint64_t owner_token,
		uint64_t lifetime_token)
{
	if (connection == NULL || request_id == 0)
	{
		return -1;
	}

	return connection->request_id == request_id
			&& connection->owner_token == owner_token
			&& connection->lifetime_token == lifetime_token ? 0 : 1;
}

ck_connection_state_t ck_connection_state(const ck_connection_t *connection)
{
	if (connection == NULL)
	{
		return CK_CONNECTION_STATE_INVALID;
	}

	return (ck_connection_state_t)atomic_load_explicit(
			&connection->state, memory_order_acquire);
}

ck_connection_terminal_event_t ck_connection_terminal_event(
		const ck_connection_t *connection)
{
	int32_t event;

	if (connection == NULL)
	{
		return -1;
	}

	event = atomic_load_explicit(&connection->terminal_event,
			memory_order_acquire);
	return (ck_connection_terminal_event_t)event;
}
