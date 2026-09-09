#include "ck_connection.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#define CK_CONNECTION_STATE_MASK UINT64_C(0xff)
#define CK_CONNECTION_EVENT_SHIFT 8u
#define CK_CONNECTION_EVENT_MASK UINT64_C(0xff)
#define CK_CONNECTION_CYCLE_SHIFT 16u
#define CK_CONNECTION_CYCLE_MASK UINT64_C(0x0000ffffffffffff)

static uint64_t ck_connection_pack(uint32_t state, int32_t event,
		uint64_t cycle_id)
{
	uint64_t event_bits = event < 0 ? UINT64_C(0xff) : (uint64_t)(uint32_t)event;
	return ((cycle_id & CK_CONNECTION_CYCLE_MASK) << CK_CONNECTION_CYCLE_SHIFT)
			| ((event_bits & CK_CONNECTION_EVENT_MASK) << CK_CONNECTION_EVENT_SHIFT)
			| ((uint64_t)state & CK_CONNECTION_STATE_MASK);
}

static uint32_t ck_connection_unpack_state(uint64_t lifecycle)
{
	return (uint32_t)(lifecycle & CK_CONNECTION_STATE_MASK);
}

static int32_t ck_connection_unpack_event(uint64_t lifecycle)
{
	return (lifecycle & (CK_CONNECTION_EVENT_MASK << CK_CONNECTION_EVENT_SHIFT))
			== (UINT64_C(0xff) << CK_CONNECTION_EVENT_SHIFT) ? -1
			: (int32_t)((lifecycle >> CK_CONNECTION_EVENT_SHIFT)
				& CK_CONNECTION_EVENT_MASK);
}

static uint64_t ck_connection_unpack_cycle(uint64_t lifecycle)
{
	return (lifecycle >> CK_CONNECTION_CYCLE_SHIFT) & CK_CONNECTION_CYCLE_MASK;
}

static int ck_connection_valid_event(ck_connection_terminal_event_t event)
{
	return event >= CK_CONNECTION_TERMINAL_COMPLETE
			&& event <= CK_CONNECTION_TERMINAL_SHUTDOWN;
}

static int ck_connection_valid_cycle_id(uint64_t cycle_id)
{
	return cycle_id != 0 && cycle_id <= CK_CONNECTION_CYCLE_MASK;
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

	connection->http_reader = malloc(sizeof(*connection->http_reader));
	if (connection->http_reader == NULL)
	{
		return -1;
	}
	connection->http_writer = malloc(sizeof(*connection->http_writer));
	if (connection->http_writer == NULL)
	{
		free(connection->http_reader);
		connection->http_reader = NULL;
		return -1;
	}

	connection->connection_id = connection_id;
	connection->request_id = request_id;
	connection->owner_token = owner_token;
	connection->lifetime_token = lifetime_token;
	connection->socket_fd = -1;
	ck_http_connection_reader_init(connection->http_reader);
	ck_http_output_writer_init(connection->http_writer);
	atomic_init(&connection->lifecycle,
			ck_connection_pack(CK_CONNECTION_OPEN, -1, 0));
	return 0;
}

int ck_connection_attach_socket(ck_connection_t *connection, int socket_fd)
{
	if (connection == NULL || socket_fd < 0)
	{
		return -1;
	}

	if (ck_connection_state(connection) != CK_CONNECTION_OPEN)
	{
		return 1;
	}

	if (connection->socket_fd >= 0)
	{
		return 1;
	}

	connection->socket_fd = socket_fd;
	if (ck_http_output_writer_attach_socket(
			connection->http_writer, socket_fd) != 0)
	{
		connection->socket_fd = -1;
		return -1;
	}
	return 0;
}

int ck_connection_socket_fd(const ck_connection_t *connection)
{
	if (connection == NULL)
	{
		return -1;
	}

	return connection->socket_fd;
}

ck_http_connection_reader_t *ck_connection_http_reader(ck_connection_t *connection)
{
	if (connection == NULL || connection->http_reader == NULL
			|| ck_connection_state(connection) == CK_CONNECTION_CLOSED)
	{
		return NULL;
	}
	return connection->http_reader;
}

ck_http_output_writer_t *ck_connection_http_writer(
	ck_connection_t *connection)
{
	if (connection == NULL || connection->http_writer == NULL
			|| ck_connection_state(connection) == CK_CONNECTION_CLOSED)
	{
		return NULL;
	}
	return connection->http_writer;
}

int ck_connection_start_async_cycle(ck_connection_t *connection,
	uint64_t cycle_id)
{
	uint64_t expected;
	uint64_t desired;

	if (connection == NULL || !ck_connection_valid_cycle_id(cycle_id))
	{
		return -1;
	}

	expected = ck_connection_pack(CK_CONNECTION_OPEN, -1, 0);
	desired = ck_connection_pack(CK_CONNECTION_ASYNC_WAIT, -1, cycle_id);
	return atomic_compare_exchange_strong_explicit(
			&connection->lifecycle, &expected, desired,
			memory_order_acq_rel, memory_order_acquire) ? 0 : 1;
}

int ck_connection_start_async(ck_connection_t *connection)
{
	return ck_connection_start_async_cycle(connection, UINT64_C(1));
}

int ck_connection_try_terminal(ck_connection_t *connection,
	ck_connection_terminal_event_t event)
{
	uint64_t current;
	uint64_t desired;
	uint32_t state;
	int32_t current_event;

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
			current_event = ck_connection_unpack_event(current);
			return current_event == (int32_t)event
					? CK_CONNECTION_TERMINAL_ALREADY_SAME
					: CK_CONNECTION_TERMINAL_ALREADY_DIFFERENT;
		}

		if (state != CK_CONNECTION_OPEN && state != CK_CONNECTION_ASYNC_WAIT)
		{
			return -1;
		}

		desired = ck_connection_pack(CK_CONNECTION_CLOSING, event,
				ck_connection_unpack_cycle(current));
		if (atomic_compare_exchange_weak_explicit(
				&connection->lifecycle, &current, desired,
				memory_order_acq_rel, memory_order_acquire))
		{
			return CK_CONNECTION_TERMINAL_CLAIMED;
		}
	}
}

int ck_connection_close(ck_connection_t *connection)
{
	uint64_t current;
	uint64_t expected;
	uint64_t desired;
	int socket_fd;
	int close_result;

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
				ck_connection_unpack_event(current),
				ck_connection_unpack_cycle(current));
		if (atomic_compare_exchange_weak_explicit(
				&connection->lifecycle, &expected, desired,
				memory_order_acq_rel, memory_order_acquire))
		{
			/* The successful state transition transfers descriptor cleanup to this caller. */
			socket_fd = connection->socket_fd;
			connection->socket_fd = -1;
			if (socket_fd >= 0)
			{
				close_result = close(socket_fd);
				if (close_result != 0)
				{
					/* The lifecycle is already CLOSED; the descriptor will not be retried after EINTR. */
					if (errno != EINTR)
					{
						free(connection->http_reader);
						connection->http_reader = NULL;
						free(connection->http_writer);
						connection->http_writer = NULL;
						return -1;
					}
				}
			}

			free(connection->http_reader);
			connection->http_reader = NULL;
			free(connection->http_writer);
			connection->http_writer = NULL;
			return close_result == 0 || socket_fd < 0 ? 0 : 2;
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

int ck_connection_validate_cycle(const ck_connection_t *connection,
	uint64_t request_id,
	uint64_t owner_token,
	uint64_t lifetime_token,
	uint64_t cycle_id)
{
	if (connection == NULL || request_id == 0
			|| !ck_connection_valid_cycle_id(cycle_id))
	{
		return -1;
	}

	if (connection->request_id != request_id
			|| connection->owner_token != owner_token
			|| connection->lifetime_token != lifetime_token)
	{
		return 1;
	}

	return ck_connection_unpack_cycle(atomic_load_explicit(
			&connection->lifecycle, memory_order_acquire)) == cycle_id ? 0 : 1;
}