#include "../../c/connection/ck_connection.h"

#include <assert.h>
#include <pthread.h>

struct race_args
{
	ck_connection_t *connection;
	ck_connection_terminal_event_t event;
	int result;
};

static void *terminal_thread(void *arg)
{
	struct race_args *args = arg;
	args->result = ck_connection_try_terminal(args->connection, args->event);
	return NULL;
}

int main(void)
{
	ck_connection_t connection;
	struct race_args complete = { 0 };
	struct race_args disconnect = { 0 };
	pthread_t complete_thread;
	pthread_t disconnect_thread;
	int winners;

	assert(ck_connection_init(&connection, 1, 11, 22, 33) == 0);
	assert(ck_connection_validate(&connection, 11, 22, 33) == 0);
	assert(ck_connection_validate(&connection, 11, 22, 34) == 1);
	assert(ck_connection_start_async_cycle(&connection, 42) == 0);
	assert(ck_connection_start_async_cycle(&connection, 42) == 1);
	assert(ck_connection_validate_cycle(&connection, 11, 22, 33, 42) == 0);
	assert(ck_connection_validate_cycle(&connection, 11, 22, 33, 43) == 1);
	assert(ck_connection_validate_cycle(&connection, 11, 22, 34, 42) == 1);

	complete.connection = &connection;
	complete.event = CK_CONNECTION_TERMINAL_COMPLETE;
	disconnect.connection = &connection;
	disconnect.event = CK_CONNECTION_TERMINAL_CLIENT_DISCONNECT;
	assert(pthread_create(&complete_thread, NULL, terminal_thread, &complete) == 0);
	assert(pthread_create(&disconnect_thread, NULL, terminal_thread, &disconnect) == 0);
	assert(pthread_join(complete_thread, NULL) == 0);
	assert(pthread_join(disconnect_thread, NULL) == 0);
	winners = (complete.result == 0) + (disconnect.result == 0);
	assert(winners == 1);
	assert(ck_connection_state(&connection) == CK_CONNECTION_CLOSING);
	assert(ck_connection_close(&connection) == 0);
	assert(ck_connection_close(&connection) == 1);
	assert(ck_connection_state(&connection) == CK_CONNECTION_CLOSED);
	assert(ck_connection_terminal_event(&connection) ==
			(complete.result == 0 ? CK_CONNECTION_TERMINAL_COMPLETE :
			CK_CONNECTION_TERMINAL_CLIENT_DISCONNECT));

	return 0;
}
