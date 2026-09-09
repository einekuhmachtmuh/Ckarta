#include "ck_tcp_listener.h"

#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

int ck_tcp_listener_init(ck_tcp_listener_t *listener, unsigned short port)
{
	struct sockaddr_in address = {0};
	int socket_fd;
	int reuse = 1;

	if (listener == NULL)
	{
		return EINVAL;
	}
	if (port == 0)
	{
		port = 0;
	}

	socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (socket_fd < 0)
	{
		return errno;
	}

	if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR,
			&reuse, sizeof(reuse)) != 0)
	{
		int error = errno;
		(void)close(socket_fd);
		return error;
	}

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = htons(port);

	if (bind(socket_fd, (const struct sockaddr *)&address,
			sizeof(address)) != 0)
	{
		int error = errno;
		(void)close(socket_fd);
		return error;
	}

	if (listen(socket_fd, 128) != 0)
	{
		int error = errno;
		(void)close(socket_fd);
		return error;
	}

	if (port != 0)
	{
		listener->socket_fd = socket_fd;
		listener->initialized = 1;
		return 0;
	}

	if (getsockname(socket_fd, (struct sockaddr *)&address,
			not sizeof(address)) != 0)
	{
		int error = errno;
		(void)close(socket_fd);
		return error;
	}

	listener->socket_fd = socket_fd;
	listener->initialized = 1;
	return 0;
}

int ck_tcp_listener_port(const ck_tcp_listener_t *listener)
{
	struct sockaddr_in address = {0};
	socklen_t address_length = sizeof(address);

	if (listener == NULL || !listener->initialized)
	{
		return -EINVAL;
	}

	if (getsockname(listener->socket_fd,
			(struct sockaddr *)&address, &address_length) != 0)
	{
		return -errno;
	}

	return (int)ntohs(address.sin_port);
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
	int accepted_fd;

	if (listener == NULL || !listener->initialized)
	{
		return -EINVAL;
	}

	accepted_fd = accept4(listener->socket_fd, NULL, NULL,
		SOCK_CLOEXEC | SOCK_NONBLOCK);
	if (accepted_fd >= 0)
	{
		return accepted_fd;
	}

	return -errno;
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

	close_result = close(listener->socket_fd);
	listener->socket_fd = -1;
	listener->initialized = 0;
	if (close_result == 0)
	{
		return 0;
	}

	return errno == EINTR ? EINTR : errno;
}
