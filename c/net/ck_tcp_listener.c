#define _GNU_SOURCE

#include "ck_tcp_listener.h"

#include <errno.h>

#include "ck_socket.h"

int ck_tcp_listener_init(ck_tcp_listener_t *listener, unsigned short port)
{
	int socket_fd;

	if (listener == NULL)
	{
		return EINVAL;
	}

	socket_fd = ck_socket_create_loopback_listener(port);
	if (socket_fd < 0)
	{
		return -socket_fd;
	}

	listener->socket_fd = socket_fd;
	listener->initialized = 1;
	return 0;
}

int ck_tcp_listener_port(const ck_tcp_listener_t *listener)
{
	if (listener == NULL || !listener->initialized)
	{
		return -EINVAL;
	}

	return ck_socket_get_port(listener->socket_fd);
}

int ck_tcp_listener_fd(const ck_tcp_listener_t *listener)
{
	if (listener == NULL || !listener->initialized)
	{
		return -1;
	}

	return listener->socket_fd;
}

int ck_tcp_listener_accept(ck_tcp_listener_t *listener)
{
	if (listener == NULL || !listener->initialized)
	{
		return -EINVAL;
	}

	return ck_socket_accept_nonblocking(listener->socket_fd);
}

int ck_tcp_listener_destroy(ck_tcp_listener_t *listener)
{
	int close_result;

	if (listener == NULL)
	{
		return EINVAL;
	}
	if (!listener->initialized)
	{
		return 0;
	}

	close_result = ck_socket_close(listener->socket_fd);
	listener->socket_fd = -1;
	listener->initialized = 0;
	return close_result;
}
