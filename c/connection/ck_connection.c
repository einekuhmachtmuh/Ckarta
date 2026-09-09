#include "ck_connection.h"

#define CK_CONNECTION_STATE_MASK UINT64_C(0xffffffff)

static uint64_t ck_connection_pack(uint32_t state, int32_t event)
{
	uint64_t event_bits = event < 0 ? UINT32_MAX : (uint32_t)event;
	return (event_bits << 32) | (uint64_t)state;
}

static uint32_t ck_connection_unpack_state(uint64_t lifecycle)
{
	return (uint32_t)(lifecycle & CK_CONNECTION_STATE_MASK);
}

static int32_t ck_connection_unpack_event(uint64_t lifecycle)
{
	return (int32_t)(uint32_t)(lifecycle >> 32);
}

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
	atomic_init(&connection->lifecycle,
			ck_connection_pack(CK_CONNECTION_OPEN, -1));
	return 0;
}

int ck_connection_start_async(ck_connection_t *connection)
{
	uint64_t expected;
	uint64_t desired;

	if (connection == NULL)
	{
		return -1;
	}

	expected = ck_connection_pack(CK_CONNECTION_OPEN, -1);
	desired = ck_connection_pack(CK_CONNECTION_ASYNC_WAIT, -1);
	return atomic_compare_exchange_strong_explicit(
			&connection->lifecycle, &expected, desired,
			memory_order_acq_rel, memory_order_acquire) ? 0 : 1;
}

int ck_connection_try_terminal(ck_connection_t *connection,
		ck_connection_terminal_event_t event)
{
	uint64_t current;
	uint64_t expected;
	uint64_t desired;
	uint32_t state;

	if (connection == NULL || !ck_connection_valid_event(event))
	{
		return -1;
	}

	current = atomic_load_explicit(&connection->lifecycle,
			memory_order_acquire);
	for (;;)
	{
		state = ck_connection_unpack_state(current);
		if (state == CK_CONNECTION_CLOSING || state == CK_CONNECTION_CLOSED)
		{
			return 1;
		}

		if (state != CK_CONNECTION_OPEN && state != CK_CONNECTION_ASYNC_WAIT)
		{
			return -1;
		}

		expected = current;
		desired = ck_connection_pack(CK_CONNECTION_CLOSING, event);
		if (atomic_compare_exchange_weak_explicit(
				&connection->lifecycle, &expected, desired,
				memory_order_acq_rel, memory_order_acquire))
		{
			return 0;
		}
		current = expected;
	}
}

int ck_connection_close(ck_connection_t *connection)
{
	uint64_t current;
	uint64_t expected;
	uint64_t desired;

	if (connection == NULL)
	{
		return -1;
	}

	current = atomic_load_explicit(&connection->lifecycle,
			memory_order_acquire);
	for (;;)
	{
		if (ck_connection_unpack_state(current) == CK_CONNECTION_CLOSED)
		{
			return 1;
		}

		if (ck_connection_unpack_state(current) != CK_CONNECTION_CLOSING
				|| ck_connection_unpack_event(current) < 0)
		{
			return 1;
		}

		expected = current;
		desired = ck_connection_pack(CK_CONNECTION_CLOSED,
				ck_connection_unpack_event(current));
		if (atomic_compare_exchange_weak_explicit(
				&connection->lifecycle, &expected, desired,
				memory_order_acq_rel, memory_order_acquire))
		{
			return 0;
		}
		current = expected;
	}
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

	return (ck_connection_state_t)ck_connection_unpack_state(
			atomic_load_explicit(&connection->lifecycle, memory_order_acquire));
}

ck_connection_terminal_event_t ck_connection_terminal_event(
		const ck_connection_t *connection)
{
	int32_t event;

	if (connection == NULL)
	{
		return -1;
	}

	event = ck_connection_unpack_event(atomic_load_explicit(
			&connection->lifecycle, memory_order_acquire));
	return (ck_connection_terminal_event_t)event;
}
