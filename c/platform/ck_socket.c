#define _GNU_SOURCE

#include "ck_socket.h"

#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

int ck_socket_is_valid(ck_socket_t socket)
{
	return socket.value != UINTPTR_MAX;
}

static int ck_socket_native_fd(ck_socket_t socket, int *socket_fd)
{
	if (socket_fd == NULL || !ck_socket_is_valid(socket)
			|| socket.value > (uintptr_t)INT_MAX)
	{
		return EINVAL;
	}

	*socket_fd = (int)socket.value;
	return 0;
}

int ck_socket_create_loopback_listener(unsigned short port,
	ck_socket_t *out_socket)
{
	struct sockaddr_in address = {0};
	int socket_fd;
	int reuse = 1;

	if (out_socket == NULL)
	{
		return EINVAL;
	}
	*out_socket = CK_SOCKET_INVALID;

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

	out_socket->value = (uintptr_t)(unsigned int)socket_fd;
	return 0;
}

int ck_socket_get_port(ck_socket_t socket)
{
	struct sockaddr_in address = {0};
	socklen_t address_length = sizeof(address);
	int socket_fd;
	int result;

	result = ck_socket_native_fd(socket, &socket_fd);
	if (result != 0)
	{
		return -result;
	}

	if (getsockname(socket_fd,
			(struct sockaddr *)&address, &address_length) != 0)
	{
		return -errno;
	}

	return (int)ntohs(address.sin_port);
}

int ck_socket_accept_nonblocking(ck_socket_t listener,
	ck_socket_t *accepted_socket)
{
	int listener_fd;
	int accepted_fd;
	int result;

	if (accepted_socket == NULL)
	{
		return EINVAL;
	}
	*accepted_socket = CK_SOCKET_INVALID;

	result = ck_socket_native_fd(listener, &listener_fd);
	if (result != 0)
	{
		return result;
	}

	accepted_fd = accept4(listener_fd, NULL, NULL,
		SOCK_CLOEXEC | SOCK_NONBLOCK);
	if (accepted_fd >= 0)
	{
		accepted_socket->value = (uintptr_t)(unsigned int)accepted_fd;
		return 0;
	}

	return errno;
}

ptrdiff_t ck_socket_recv_nonblocking(
	ck_socket_t socket,
	void *buffer,
	size_t length)
{
	int socket_fd;

	if (ck_socket_native_fd(socket, &socket_fd) != 0
			|| (buffer == NULL && length != 0))
	{
		errno = EINVAL;
		return -1;
	}

	return (ptrdiff_t)recv(socket_fd, buffer, length, MSG_DONTWAIT);
}

ptrdiff_t ck_socket_send_nonblocking(
	ck_socket_t socket,
	const void *buffer,
	size_t length)
{
	int socket_fd;

	if (ck_socket_native_fd(socket, &socket_fd) != 0
			|| (buffer == NULL && length != 0))
	{
		errno = EINVAL;
		return -1;
	}

	return (ptrdiff_t)send(socket_fd, buffer, length,
		MSG_DONTWAIT | MSG_NOSIGNAL);
}

int ck_socket_close(ck_socket_t socket)
{
	int socket_fd;

	if (ck_socket_native_fd(socket, &socket_fd) != 0)
	{
		return EINVAL;
	}
	return close(socket_fd) == 0 ? 0 : errno;
}
