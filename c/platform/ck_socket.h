#ifndef CKARTA_SOCKET_H
#define CKARTA_SOCKET_H

#include <stddef.h>
#include <stdint.h>

typedef struct ck_socket
{
	uintptr_t value;
} ck_socket_t;

#define CK_SOCKET_INVALID ((ck_socket_t){UINTPTR_MAX})

int ck_socket_is_valid(ck_socket_t socket);
int ck_socket_create_loopback_listener(unsigned short port,
	ck_socket_t *socket);
int ck_socket_get_port(ck_socket_t socket);
int ck_socket_accept_nonblocking(ck_socket_t listener,
	ck_socket_t *accepted_socket);
ptrdiff_t ck_socket_recv_nonblocking(
	ck_socket_t socket,
	void *buffer,
	size_t length);
ptrdiff_t ck_socket_send_nonblocking(
	ck_socket_t socket,
	const void *buffer,
	size_t length);
int ck_socket_close(ck_socket_t socket);

#endif
