#define _GNU_SOURCE

#include "ck_socket.h"

#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

int ck_socket_create_loopback_listener(unsigned short port)
{
	struct sockaddr_in address = {0};
	int socket_fd;
	int reuse = 1;

	socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (socket_fd < 0)
	{
		return -errno;
	}

	if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR,
		&reuse, sizeof(reuse)) != 0)
	{
		int error = errno;
		(void)close(socket_fd);
		return -error;
	}

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = htons(port);
	if (bind(socket_fd, (const struct sockaddr *)&address,
			sizeof(address)) != 0)
	{
		int error = errno;
		(void)close(socket_fd);
		return -error;
	}

	if (listen(socket_fd, 128) != 0)
	{
		int error = errno;
		(void)close(socket_fd);
		return -error;
	}

	return socket_fd;
}

int ck_socket_get_port(int socket_fd)
{
	struct sockaddr_in address = {0};
	socklen_t address_length = sizeof(address);

	if (socket_fd < 0)
	{
		return -EINVAL;
	}
	if (getsockname(socket_fd,
		(struct sockaddr *)&address, &address_length) != 0)
	{
		return -errno;
	}

	return (int)ntohs(address.sin_port);
}

int ck_socket_accept_nonblocking(int socket_fd)
{
	int accepted_fd;

	if (socket_fd < 0)
	{
		return -EINVAL;
	}

	accepted_fd = accept4(socket_fd, NULL, NULL,
		SOCK_CLOEXEC | SOCK_NONBLOCK);
	if (accepted_fd >= 0)
	{
		return accepted_fd;
	}

	return -errno;
}

ssize_t ck_socket_recv_nonblocking(
	int socket_fd,
	void *buffer,
	size_t length)
{
	if (socket_fd < 0 || (buffer == NULL && length != 0))
	{
		errno = EINVAL;
		return -1;
	}

	return recv(socket_fd, buffer, length, MSG_DONTWAIT);
}

ssize_t ck_socket_send_nonblocking(
	int socket_fd,
	const void *buffer,
	size_t length)
{
	if (socket_fd < 0 || (buffer == NULL && length != 0))
	{
		errno = EINVAL;
		return -1;
	}

	return send(socket_fd, buffer, length,
		MSG_DONTWAIT | MSG_NOSIGNAL);
}

int ck_socket_close(int socket_fd)
{
	if (socket_fd < 0)
	{
		return EINVAL;
	}
	if (close(socket_fd) == 0)
	{
		return 0;
	}
	return errno == EINTR ? EINTR : errno;
}
