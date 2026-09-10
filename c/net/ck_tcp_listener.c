#include "ck_tcp_listener.h"

#include <errno.h>

int ck_tcp_listener_init(ck_tcp_listener_t *listener, unsigned short port)
{
	if (listener == NULL)
	{
		return EINVAL;
	}

	listener->socket = CK_SOCKET_INVALID;
	listener->initialized = 0;
	if (ck_socket_create_loopback_listener(port, &listener->socket) != 0)
	{
		return EIO;
	}

	listener->initialized = 1;
	return 0;
}

int ck_tcp_listener_port(const ck_tcp_listener_t *listener)
{
	if (listener == NULL || !listener->initialized)
	{
		return -EINVAL;
	}

	return ck_socket_get_port(listener->socket);
}

ck_socket_t ck_tcp_listener_socket(const ck_tcp_listener_t *listener)
{
	if (listener == NULL || !listener->initialized)
	{
		return CK_SOCKET_INVALID;
	}

	return listener->socket;
}

int ck_tcp_listener_accept(ck_tcp_listener_t *listener,
	ck_socket_t *accepted_socket)
{
	if (listener == NULL || !listener->initialized)
	{
		return EINVAL;
	}

	return ck_socket_accept_nonblocking(listener->socket, accepted_socket);
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

	close_result = ck_socket_close(listener->socket);
	listener->socket = CK_SOCKET_INVALID;
	listener->initialized = 0;
	return close_result;
}
