#ifndef CKARTA_SOCKET_H
#define CKARTA_SOCKET_H

#include <stddef.h>
#include <sys/types.h>

int ck_socket_create_loopback_listener(unsigned short port);
int ck_socket_get_port(int socket_fd);
int ck_socket_accept_nonblocking(int socket_fd);
ssize_t ck_socket_recv_nonblocking(
	int socket_fd,
	void *buffer,
	size_t length);
ssize_t ck_socket_send_nonblocking(
	int socket_fd,
	const void *buffer,
	size_t length);
int ck_socket_close(int socket_fd);

#endif
