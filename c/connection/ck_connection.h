#ifndef CKARTA_CONNECTION_H
#define CKARTA_CONNECTION_H

#include <stdatomic.h>
#include <stdint.h>

#include "../http/ck_http_connection_reader.h"

typedef enum ck_connection_state
{
	CK_CONNECTION_OPEN = 0,
	CK_CONNECTION_ASYNC_WAIT = 1,
	CK_CONNECTION_CLOSING = 2,
	CK_CONNECTION_CLOSED = 3,
	CK_CONNECTION_STATE_INVALID = -1
} ck_connection_state_t;

typedef enum ck_connection_terminal_event
{
	CK_CONNECTION_TERMINAL_COMPLETE = 0,
	CK_CONNECTION_TERMINAL_CLIENT_DISCONNECT = 1,
	CK_CONNECTION_TERMINAL_TIMEOUT = 2,
	CK_CONNECTION_TERMINAL_ERROR = 3,
	CK_CONNECTION_TERMINAL_SHUTDOWN = 4
} ck_connection_terminal_event_t;

typedef enum ck_connection_terminal_result
{
	CK_CONNECTION_TERMINAL_CLAIMED = 0,
	CK_CONNECTION_TERMINAL_ALREADY_SAME = 1,
	CK_CONNECTION_TERMINAL_ALREADY_DIFFERENT = 2
} ck_connection_terminal_result_t;

typedef struct ck_connection
{
	uint64_t connection_id;
	uint64_t request_id;
	uint64_t owner_token;
	uint64_t lifetime_token;
	int socket_fd;
	ck_http_connection_reader_t *http_reader;
	/* Low 8 bits are state; next 8 bits are terminal event; upper 48 bits are cycle id. */
	_Atomic uint64_t lifecycle;
} ck_connection_t;

int ck_connection_init(ck_connection_t *connection,
		uint64_t connection_id,
		uint64_t request_id,
		uint64_t owner_token,
		uint64_t lifetime_token);
int ck_connection_attach_socket(ck_connection_t *connection, int socket_fd);
int ck_connection_socket_fd(const ck_connection_t *connection);
ck_http_connection_reader_t *ck_connection_http_reader(ck_connection_t *connection);
int ck_connection_start_async(ck_connection_t *connection);
int ck_connection_start_async_cycle(ck_connection_t *connection,
		uint64_t cycle_id);
int ck_connection_try_terminal(ck_connection_t *connection,
		ck_connection_terminal_event_t event);
int ck_connection_close(ck_connection_t *connection);
int ck_connection_validate(const ck_connection_t *connection,
		uint64_t request_id,
		uint64_t owner_token,
		uint64_t lifetime_token);
int ck_connection_validate_cycle(const ck_connection_t *connection,
		uint64_t request_id,
		uint64_t owner_token,
		uint64_t lifetime_token,
		uint64_t cycle_id);
ck_connection_state_t ck_connection_state(const ck_connection_t *connection);
ck_connection_terminal_event_t ck_connection_terminal_event(
		const ck_connection_t *connection);

#endif