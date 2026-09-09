#include "../../c/connection/ck_connection.h"

#include <assert.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

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
	int socket_pair[2];
	char byte;
	int winners;

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, socket_pair) == 0);
	assert(ck_connection_init(&connection, 1, 11, 22, 33) == 0);
	assert(ck_connection_socket_fd(&connection) == -1);
	assert(ck_connection_attach_socket(&connection, socket_pair[0]) == 0);
	assert(ck_connection_socket_fd(&connection) == socket_pair[0]);
	assert(ck_connection_attach_socket(&connection, socket_pair[1]) == 1);
	assert(ck_connection_validate(&connection, 11, 22, 33) == 0);
	assert(ck_connection_validate(&connection, 11, 22, 34) == 1);
	assert(ck_connection_start_async_cycle(&connection, 42) == 0);
	assert(ck_connection_start_async_cycle(&connection, 42) == 1);
	assert(ck_connection_validate_cycle(&connection, 11, 22, 33, 42) == 0);
	assert(ck_connection_validate_cycle(&connection, 11, 22, 33, 43) == 1);
	assert(ck_connection_validate_cycle(&connection, 11, 22, 34, 42) == 1);

	assert(ck_connection_try_terminal(
			&connection, CK_CONNECTION_TERMINAL_COMPLETE)
			== CK_CONNECTION_TERMINAL_CLAIMED);
	assert(ck_connection_try_terminal(
			&connection, CK_CONNECTION_TERMINAL_COMPLETE)
			== CK_CONNECTION_TERMINAL_ALREADY_SAME);
	assert(ck_connection_try_terminal(
			&connection, CK_CONNECTION_TERMINAL_TIMEOUT)
			== CK_CONNECTION_TERMINAL_ALREADY_DIFFERENT);
	assert(ck_connection_close(&connection) == 0);
	assert(ck_connection_socket_fd(&connection) == -1);
	assert(ck_connection_close(&connection) == 1);
	assert(ck_connection_state(&connection) == CK_CONNECTION_CLOSED);
	assert(recv(socket_pair[1], &byte, 1, 0) == 0);
	assert(close(socket_pair[1]) == 0);

	assert(ck_connection_init(&connection, 2, 21, 22, 23) == 0);
	assert(ck_connection_start_async(&connection) == 0);
	complete.connection = &connection;
	complete.event = CK_CONNECTION_TERMINAL_COMPLETE;
	disconnect.connection = &connection;
	disconnect.event = CK_CONNECTION_TERMINAL_CLIENT_DISCONNECT;
	assert(pthread_create(&complete_thread, NULL, terminal_thread, &complete) == 0);
	assert(pthread_create(&disconnect_thread, NULL, terminal_thread, &disconnect) == 0);
	assert(pthread_join(complete_thread, NULL) == 0);
	assert(pthread_join(disconnect_thread, NULL) == 0);
	winners = (complete.result == CK_CONNECTION_TERMINAL_CLAIMED)
			+ (disconnect.result == CK_CONNECTION_TERMINAL_CLAIMED);
	assert(winners == 1);
	assert((complete.result == CK_CONNECTION_TERMINAL_ALREADY_SAME)
			|| (complete.result == CK_CONNECTION_TERMINAL_ALREADY_DIFFERENT)
			|| complete.result == CK_CONNECTION_TERMINAL_CLAIMED);
	assert((disconnect.result == CK_CONNECTION_TERMINAL_ALREADY_SAME)
			|| (disconnect.result == CK_CONNECTION_TERMINAL_ALREADY_DIFFERENT)
			|| disconnect.result == CK_CONNECTION_TERMINAL_CLAIMED);
	assert(ck_connection_close(&connection) == 0);
	return 0;
}
