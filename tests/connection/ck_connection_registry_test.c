#include "../../c/connection/ck_connection_registry.h"

#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
	ck_connection_registry_t registry;
	ck_connection_handle_t handles[CK_CONNECTION_REGISTRY_CAPACITY];
	ck_connection_handle_t first;
	ck_connection_handle_t replacement;
	int socket_pair[2];
	char byte;
	unsigned int i;

	assert(ck_connection_registry_init(&registry) == 0);
	assert(ck_connection_registry_active_count(&registry) == 0);

	assert(ck_connection_registry_register(
			&registry, 1, 11, 22, 33, &first) == 0);
	assert(first != 0);
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, socket_pair) == 0);
	assert(ck_connection_registry_attach_socket(
			&registry, first, 11, 22, 33, socket_pair[0]) == 0);
	assert(ck_connection_registry_socket_fd(
			&registry, first, 11, 22, 33) == socket_pair[0]);
	assert(ck_connection_registry_attach_socket(
			&registry, first, 11, 22, 34, socket_pair[1]) == -3);
	assert(ck_connection_registry_start_async_cycle(
			&registry, first, 11, 22, 33, 42) == 0);
	assert(ck_connection_registry_start_async_cycle(
			&registry, first, 11, 22, 33, 42) == 1);
	assert(ck_connection_registry_start_async_cycle(
			&registry, first, 11, 22, 34, 42) == -3);
	assert(ck_connection_registry_try_terminal(
			&registry, first, 11, 22, 33, 42,
			CK_CONNECTION_TERMINAL_COMPLETE) == 0);
	assert(ck_connection_registry_try_terminal(
			&registry, first, 11, 22, 33, 42,
			CK_CONNECTION_TERMINAL_COMPLETE) == 1);
	assert(ck_connection_registry_try_terminal(
			&registry, first, 11, 22, 33, 42,
			CK_CONNECTION_TERMINAL_TIMEOUT) == 2);
	assert(ck_connection_registry_close(
			&registry, first, 11, 22, 33) == 0);
	assert(ck_connection_registry_socket_fd(
			&registry, first, 11, 22, 33) == -1);
	assert(recv(socket_pair[1], &byte, 1, 0) == 0);
	assert(close(socket_pair[1]) == 0);
	assert(ck_connection_registry_retire(
			&registry, first, 11, 22, 33) == 0);
	assert(ck_connection_registry_retire(
			&registry, first, 11, 22, 33) == -2);

	assert(ck_connection_registry_register(
			&registry, 2, 44, 55, 66, &replacement) == 0);
	assert(replacement != first);
	assert(ck_connection_registry_start_async_cycle(
			&registry, first, 44, 55, 66, 1) == -2);
	assert(ck_connection_registry_start_async_cycle(
			&registry, replacement, 44, 55, 66, 1) == 0);
	assert(ck_connection_registry_try_terminal(
			&registry, replacement, 44, 55, 66, 1,
			CK_CONNECTION_TERMINAL_COMPLETE) == 0);
	assert(ck_connection_registry_close(
			&registry, replacement, 44, 55, 66) == 0);
	assert(ck_connection_registry_retire(
			&registry, replacement, 44, 55, 66) == 0);

	for (i = 0; i < CK_CONNECTION_REGISTRY_CAPACITY; i++)
	{
		assert(ck_connection_registry_register(
				&registry,
				1000 + i,
				1000 + i,
				2000 + i,
				3000 + i,
				&handles[i]) == 0);
		assert(ck_connection_registry_start_async_cycle(
				&registry,
				handles[i],
				1000 + i,
				2000 + i,
				3000 + i,
				1) == 0);
	}
	assert(ck_connection_registry_active_count(&registry)
			== (int)CK_CONNECTION_REGISTRY_CAPACITY);
	assert(ck_connection_registry_register(
			&registry, 999, 999, 999, 999, &first) == ENOSPC);
	assert(ck_connection_registry_destroy(&registry) == 1);

	for (i = 0; i < CK_CONNECTION_REGISTRY_CAPACITY; i++)
	{
		assert(ck_connection_registry_try_terminal(
				&registry,
				handles[i],
				1000 + i,
				2000 + i,
				3000 + i,
				1,
				CK_CONNECTION_TERMINAL_COMPLETE) == 0);
		assert(ck_connection_registry_close(
				&registry,
				handles[i],
				1000 + i,
				2000 + i,
				3000 + i) == 0);
		assert(ck_connection_registry_retire(
				&registry,
				handles[i],
				1000 + i,
				2000 + i,
				3000 + i) == 0);
	}

	assert(ck_connection_registry_active_count(&registry) == 0);
	assert(ck_connection_registry_destroy(&registry) == 0);
	return 0;
}
