#include "ck_http_connection_reader.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>

static ck_http_connection_read_result_t map_input_result(
	ck_http_input_result_t result)
{
	switch (result)
	{
	case CK_HTTP_INPUT_INCOMPLETE:
	case CK_HTTP_INPUT_BODY:
		return CK_HTTP_CONNECTION_READ_INCOMPLETE;
	case CK_HTTP_INPUT_COMPLETE:
		return CK_HTTP_CONNECTION_READ_REQUEST_COMPLETE;
	case CK_HTTP_INPUT_TOO_LARGE:
		return CK_HTTP_CONNECTION_READ_TOO_LARGE;
	case CK_HTTP_INPUT_EOF_INCOMPLETE:
		return CK_HTTP_CONNECTION_READ_EOF_INCOMPLETE;
	case CK_HTTP_INPUT_BAD_REQUEST:
	default:
		return CK_HTTP_CONNECTION_READ_BAD_REQUEST;
	}
}

void ck_http_connection_reader_init(ck_http_connection_reader_t *reader)
{
	if (reader == NULL)
	{
		return;
	}
	ck_http_input_init(&reader->input);
	reader->begin = 0;
	reader->end = 0;
}

ck_http_connection_read_result_t ck_http_connection_reader_drive(
	ck_http_connection_reader_t *reader,
	int socket_fd,
	ck_http_body_sink_fn body_sink,
	void *body_sink_context)
{
	size_t read_budget = CK_HTTP_CONNECTION_READ_BUDGET_BYTES;
	size_t process_budget = CK_HTTP_CONNECTION_PROCESS_BUDGET_BYTES;

	if (reader == NULL || socket_fd < 0)
	{
		return CK_HTTP_CONNECTION_READ_IO_ERROR;
	}

	for (;;)
	{
		while (reader->begin < reader->end)
		{
			size_t available = reader->end - reader->begin;
			size_t feed_length;
			size_t consumed = 0;
			size_t body_length = 0;
			const unsigned char *body_data = NULL;
			ck_http_input_result_t input_result;
			ck_http_connection_read_result_t read_result;

			if (process_budget == 0)
			{
				return CK_HTTP_CONNECTION_READ_INCOMPLETE;
			}

			feed_length = available < process_budget ? available : process_budget;
			input_result = ck_http_input_feed(
					&reader->input,
					reader->buffer + reader->begin,
					feed_length,
					&consumed,
					&body_data,
					&body_length);

			if (consumed > feed_length)
			{
				return CK_HTTP_CONNECTION_READ_IO_ERROR;
			}
			if (body_length != 0)
			{
				size_t body_consumed = 0;

				if (body_sink == NULL
						|| body_sink(body_sink_context, body_data, body_length) != 0)
				{
					return CK_HTTP_CONNECTION_READ_SINK_ERROR;
				}
				if (ck_http_input_ack_body(
						&reader->input, &body_consumed) != 0
						|| body_consumed > feed_length - consumed)
				{
					return CK_HTTP_CONNECTION_READ_IO_ERROR;
				}
				consumed += body_consumed;
			}
			reader->begin += consumed;
			process_budget -= consumed;

			if (ck_http_input_complete(&reader->input))
			{
				return CK_HTTP_CONNECTION_READ_REQUEST_COMPLETE;
			}

			read_result = map_input_result(input_result);
			if (read_result == CK_HTTP_CONNECTION_READ_REQUEST_COMPLETE
					|| read_result == CK_HTTP_CONNECTION_READ_BAD_REQUEST
					|| read_result == CK_HTTP_CONNECTION_READ_TOO_LARGE)
			{
				return read_result;
			}
			if (input_result == CK_HTTP_INPUT_EOF_INCOMPLETE)
			{
				return read_result;
			}
			if (reader->begin == reader->end)
			{
				break;
			}
			if (consumed == 0)
			{
				break;
			}
		}

		if (reader->begin < reader->end && process_budget == 0)
		{
			return CK_HTTP_CONNECTION_READ_INCOMPLETE;
		}

		if (reader->end == sizeof(reader->buffer))
		{
			if (reader->begin == 0)
			{
				return CK_HTTP_CONNECTION_READ_TOO_LARGE;
			}
			{
				size_t remaining = reader->end - reader->begin;
				memmove(reader->buffer,
						reader->buffer + reader->begin, remaining);
				reader->begin = 0;
				reader->end = remaining;
			}
		}

		if (read_budget == 0)
		{
			return CK_HTTP_CONNECTION_READ_INCOMPLETE;
		}

		{
			size_t available = sizeof(reader->buffer) - reader->end;
			size_t receive_length = available < read_budget ? available : read_budget;
			ssize_t received = recv(socket_fd,
					reader->buffer + reader->end,
					receive_length,
					MSG_DONTWAIT);
			if (received > 0)
			{
				reader->end += (size_t)received;
				read_budget -= (size_t)received;
				continue;
			}
			if (received == 0)
			{
				const unsigned char *unused_body = NULL;
				size_t unused_body_length = 0;
				ck_http_input_result_t eof_result = ck_http_input_eof(
						&reader->input, &unused_body, &unused_body_length);
				return map_input_result(eof_result);
			}
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				return CK_HTTP_CONNECTION_READ_INCOMPLETE;
			}
			if (errno == EINTR)
			{
				continue;
			}
			return CK_HTTP_CONNECTION_READ_IO_ERROR;
		}
	}
}

int ck_http_connection_reader_next_request(
	ck_http_connection_reader_t *reader)
{
	size_t remaining;

	if (reader == NULL || !ck_http_input_complete(&reader->input))
	{
		return -1;
	}

	remaining = reader->end - reader->begin;
	if (remaining != 0)
	{
		memmove(reader->buffer, reader->buffer + reader->begin, remaining);
	}
	reader->begin = 0;
	reader->end = remaining;
	ck_http_input_next_request(&reader->input);
	return 0;
}

const ck_http_request_t *ck_http_connection_reader_request(
	const ck_http_connection_reader_t *reader)
{
	return reader == NULL ? NULL : ck_http_input_request(&reader->input);
}

size_t ck_http_connection_reader_buffered_bytes(
	const ck_http_connection_reader_t *reader)
{
	if (reader == NULL || reader->end < reader->begin)
	{
		return 0;
	}
	return reader->end - reader->begin;
}