#include "ck_http_output_writer.h"

#include <errno.h>
#include <string.h>

#include "ck_socket.h"

void ck_http_output_writer_init(ck_http_output_writer_t *writer)
{
	if (writer == NULL)
	{
		return;
	}

	memset(writer, 0, sizeof(*writer));
	writer->socket_fd = -1;
}

int ck_http_output_writer_attach_socket(
	ck_http_output_writer_t *writer,
	int socket_fd)
{
	if (writer == NULL || socket_fd < 0 || writer->socket_fd >= 0)
	{
		return -1;
	}

	writer->socket_fd = socket_fd;
	return 0;
}

int ck_http_output_writer_queue(
	ck_http_output_writer_t *writer,
	const unsigned char *data,
	size_t length)
{
	size_t capacity;

	if (writer == NULL || writer->socket_fd < 0
			|| (data == NULL && length != 0))
	{
		return -1;
	}

	if (writer->begin > 0 && writer->end == sizeof(writer->buffer))
	{
		size_t remaining = writer->end - writer->begin;
		memmove(writer->buffer, writer->buffer + writer->begin, remaining);
		writer->begin = 0;
		writer->end = remaining;
	}

	capacity = sizeof(writer->buffer) - writer->end;
	if (length > capacity)
	{
		return -2;
	}

	if (length != 0)
	{
		memcpy(writer->buffer + writer->end, data, length);
		writer->end += length;
	}
	return 0;
}

ck_http_output_write_result_t ck_http_output_writer_drive(
	ck_http_output_writer_t *writer)
{
	size_t budget = CK_HTTP_OUTPUT_WRITE_BUDGET_BYTES;

	if (writer == NULL || writer->socket_fd < 0)
	{
		return CK_HTTP_OUTPUT_WRITE_IO_ERROR;
	}

	while (writer->begin < writer->end && budget != 0)
	{
		size_t pending = writer->end - writer->begin;
		size_t attempt = pending < budget ? pending : budget;
		ssize_t sent;

		sent = ck_socket_send_nonblocking(
				writer->socket_fd,
				writer->buffer + writer->begin,
				attempt);
		if (sent > 0)
		{
			writer->begin += (size_t)sent;
			budget -= (size_t)sent;
			continue;
		}
		if (sent == 0)
		{
			return CK_HTTP_OUTPUT_WRITE_PEER_CLOSED;
		}
		if (errno == EINTR)
		{
			continue;
		}
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			return CK_HTTP_OUTPUT_WRITE_NEED_WRITE;
		}
		if (errno == EPIPE || errno == ECONNRESET)
		{
			return CK_HTTP_OUTPUT_WRITE_PEER_CLOSED;
		}
		return CK_HTTP_OUTPUT_WRITE_IO_ERROR;
	}

	if (writer->begin == writer->end)
	{
		writer->begin = 0;
		writer->end = 0;
		return CK_HTTP_OUTPUT_WRITE_DRAINED;
	}

	return CK_HTTP_OUTPUT_WRITE_NEED_WRITE;
}

size_t ck_http_output_writer_buffered_bytes(
	const ck_http_output_writer_t *writer)
{
	if (writer == NULL || writer->end < writer->begin)
	{
		return 0;
	}
	return writer->end - writer->begin;
}
