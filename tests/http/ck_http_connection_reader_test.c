#define _GNU_SOURCE

#include "../../c/http/ck_http_connection_reader.h"
#include "../../c/http/ck_http_request_body.h"

#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct body_capture
{
	unsigned char data[64];
	size_t length;
} body_capture_t;

typedef struct body_counter
{
	size_t length;
} body_counter_t;

typedef struct body_sender
{
	int socket_fd;
	const unsigned char *data;
	size_t length;
} body_sender_t;

static int capture_body(void *context, const unsigned char *data, size_t length)
{
	body_capture_t *capture = context;

	assert(capture != NULL);
	assert(capture->length + length <= sizeof(capture->data));
	memcpy(capture->data + capture->length, data, length);
	capture->length += length;
	return 0;
}

static int count_body(void *context, const unsigned char *data, size_t length)
{
	body_counter_t *counter = context;

	assert(counter != NULL);
	(void)data;
	counter->length += length;
	return 0;
}

static void send_all(int socket_fd, const unsigned char *data, size_t length)
{
	size_t offset = 0;

	while (offset < length)
	{
		ssize_t sent = send(socket_fd, data + offset, length - offset, 0);
		assert(sent > 0);
		offset += (size_t)sent;
	}
}

static void *send_body_thread(void *context)
{
	body_sender_t *sender = context;

	assert(sender != NULL);
	send_all(sender->socket_fd, sender->data, sender->length);
	assert(shutdown(sender->socket_fd, SHUT_WR) == 0);
	return NULL;
}

typedef struct retrying_body_sink
{
	size_t calls;
	size_t bytes;
} retrying_body_sink_t;

static int reject_first_body(void *context,
	const unsigned char *data, size_t length)
{
	retrying_body_sink_t *sink = context;

	assert(sink != NULL);
	assert(data != NULL);
	sink->calls++;
	if (sink->calls == 1)
	{
		return 1;
	}
	sink->bytes += length;
	return 0;
}

static void test_body_sink_retry_does_not_replay(void)
{
	static const char payload[] =
		"POST /retry HTTP/1.1\r\n"
		"Host: localhost\r\n"
		"Content-Length: 5\r\n"
		"\r\n"
		"hello";
	ck_http_connection_reader_t reader;
	retrying_body_sink_t sink = {0};
	int sockets[2];
	ck_http_connection_read_result_t result;
	const ck_http_request_t *request;

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
	ck_http_connection_reader_init(&reader);
	assert(send(sockets[0], payload, sizeof(payload) - 1U, 0)
			== (ssize_t)(sizeof(payload) - 1U));

	result = ck_http_connection_reader_drive(&reader, sockets[1],
		reject_first_body, &sink);
	assert(result == CK_HTTP_CONNECTION_READ_SINK_ERROR);
	assert(sink.calls == 1);
	assert(sink.bytes == 0);
	assert(ck_http_connection_reader_buffered_bytes(&reader)
			>= sizeof("hello") - 1U);

	result = ck_http_connection_reader_drive(&reader, sockets[1],
		reject_first_body, &sink);
	assert(result == CK_HTTP_CONNECTION_READ_REQUEST_COMPLETE);
	assert(sink.calls == 2);
	assert(sink.bytes == 5);
	request = ck_http_connection_reader_request(&reader);
	assert(request != NULL);
	assert(request->target.length == 6);
	assert(memcmp(request->target.data, "/retry", 6) == 0);

	assert(close(sockets[0]) == 0);
	assert(close(sockets[1]) == 0);
}

static void test_content_length_pipeline(void)
{
	static const char payload[] =
		"POST /upload HTTP/1.1\r\n"
		"Host: localhost\r\n"
		"Content-Length: 5\r\n"
		"\r\n"
		"hello"
		"GET /next HTTP/1.1\r\n"
		"Host: localhost\r\n"
		"\r\n";
	ck_http_connection_reader_t reader;
	body_capture_t capture = {0};
	int sockets[2];
	ck_http_connection_read_result_t result;
	const ck_http_request_t *request;

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
	ck_http_connection_reader_init(&reader);
	assert(send(sockets[0], payload, sizeof(payload) - 1U, 0)
			== (ssize_t)(sizeof(payload) - 1U));

	result = ck_http_connection_reader_drive(&reader, sockets[1],
			capture_body, &capture);
	assert(result == CK_HTTP_CONNECTION_READ_REQUEST_COMPLETE);
	request = ck_http_connection_reader_request(&reader);
	assert(request != NULL);
	assert(request->target.length == 7);
	assert(memcmp(request->target.data, "/upload", 7) == 0);
	assert(capture.length == 5);
	assert(memcmp(capture.data, "hello", 5) == 0);
	assert(ck_http_connection_reader_buffered_bytes(&reader)
			== strlen("GET /next HTTP/1.1\r\nHost: localhost\r\n\r\n"));

	assert(ck_http_connection_reader_next_request(&reader) == 0);
	capture.length = 0;
	result = ck_http_connection_reader_drive(&reader, sockets[1],
			capture_body, &capture);
	assert(result == CK_HTTP_CONNECTION_READ_REQUEST_COMPLETE);
	request = ck_http_connection_reader_request(&reader);
	assert(request != NULL);
	assert(request->target.length == 5);
	assert(memcmp(request->target.data, "/next", 5) == 0);
	assert(capture.length == 0);

	assert(shutdown(sockets[0], SHUT_WR) == 0);
	result = ck_http_connection_reader_drive(&reader, sockets[1],
			capture_body, &capture);
	assert(result == CK_HTTP_CONNECTION_READ_REQUEST_COMPLETE);

	assert(close(sockets[0]) == 0);
	assert(close(sockets[1]) == 0);
}

static void test_chunked_stream(void)
{
	static const char payload[] =
		"POST /chunked HTTP/1.1\r\n"
		"Transfer-Encoding: chunked\r\n"
		"\r\n"
		"4\r\nWiki\r\n"
		"5;ext=yes\r\npedia\r\n"
		"0\r\nX-Trailer: yes\r\n\r\n";
	ck_http_connection_reader_t reader;
	body_capture_t capture = {0};
	int sockets[2];
	ck_http_connection_read_result_t result;
	const ck_http_request_t *request;

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
	ck_http_connection_reader_init(&reader);
	assert(send(sockets[0], payload, sizeof(payload) - 1U, 0)
			== (ssize_t)(sizeof(payload) - 1U));

	result = ck_http_connection_reader_drive(&reader, sockets[1],
			capture_body, &capture);
	assert(result == CK_HTTP_CONNECTION_READ_REQUEST_COMPLETE);
	request = ck_http_connection_reader_request(&reader);
	assert(request != NULL);
	assert(request->body_mode == CK_HTTP_BODY_CHUNKED);
	assert(capture.length == 9);
	assert(memcmp(capture.data, "Wikipedia", 9) == 0);
	assert(ck_http_connection_reader_buffered_bytes(&reader) == 0);

	assert(shutdown(sockets[0], SHUT_WR) == 0);
	result = ck_http_connection_reader_drive(&reader, sockets[1],
			capture_body, &capture);
	assert(result == CK_HTTP_CONNECTION_READ_REQUEST_COMPLETE);

	assert(close(sockets[0]) == 0);
	assert(close(sockets[1]) == 0);
}

static void test_chunked_split_crlf(void)
{
	static const char first[] =
		"POST /split HTTP/1.1\r\n"
		"Transfer-Encoding: chunked\r\n"
		"\r\n"
		"4\r\nWiki\r";
	static const char second[] =
		"\n"
		"0\r\n\r\n";
	ck_http_connection_reader_t reader;
	body_capture_t capture = {0};
	int sockets[2];
	ck_http_connection_read_result_t result;
	const ck_http_request_t *request;

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
	ck_http_connection_reader_init(&reader);
	assert(send(sockets[0], first, sizeof(first) - 1U, 0)
			== (ssize_t)(sizeof(first) - 1U));

	result = ck_http_connection_reader_drive(&reader, sockets[1],
			capture_body, &capture);
	assert(result == CK_HTTP_CONNECTION_READ_INCOMPLETE);
	assert(capture.length == 4);
	assert(memcmp(capture.data, "Wiki", 4) == 0);

	assert(send(sockets[0], second, sizeof(second) - 1U, 0)
			== (ssize_t)(sizeof(second) - 1U));
	result = ck_http_connection_reader_drive(&reader, sockets[1],
			capture_body, &capture);
	assert(result == CK_HTTP_CONNECTION_READ_REQUEST_COMPLETE);
	request = ck_http_connection_reader_request(&reader);
	assert(request != NULL);
	assert(request->target.length == 6);
	assert(memcmp(request->target.data, "/split", 6) == 0);
	assert(capture.length == 4);
	assert(memcmp(capture.data, "Wiki", 4) == 0);

	assert(close(sockets[0]) == 0);
	assert(close(sockets[1]) == 0);
}

static void test_read_batch_budget(void)
{
	static const char prefix[] =
		"POST /large HTTP/1.1\r\n"
		"Content-Length: 40000\r\n\r\n";
	static unsigned char body[40000];
	ck_http_connection_reader_t reader;
	body_counter_t counter = {0};
	int sockets[2];
	ck_http_connection_read_result_t result;

	for (size_t i = 0; i < sizeof(body); i++)
	{
		body[i] = (unsigned char)('a' + (i % 26U));
	}

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
	ck_http_connection_reader_init(&reader);
	send_all(sockets[0], (const unsigned char *)prefix, sizeof(prefix) - 1U);
	send_all(sockets[0], body, sizeof(body));

	result = ck_http_connection_reader_drive(&reader, sockets[1],
			count_body, &counter);
	assert(result == CK_HTTP_CONNECTION_READ_INCOMPLETE);
	assert(counter.length > 0);
	assert(counter.length < sizeof(body));

	result = ck_http_connection_reader_drive(&reader, sockets[1],
			count_body, &counter);
	assert(result == CK_HTTP_CONNECTION_READ_REQUEST_COMPLETE);
	assert(counter.length == sizeof(body));
	assert(ck_http_connection_reader_buffered_bytes(&reader) == 0);

	assert(close(sockets[0]) == 0);
	assert(close(sockets[1]) == 0);
}

static void test_eof_incomplete_body(void)
{
	static const char header[] =
		"POST /x HTTP/1.1\r\n"
		"Content-Length: 5\r\n\r\n";
	ck_http_connection_reader_t reader;
	body_capture_t capture = {0};
	int sockets[2];
	ck_http_connection_read_result_t result;

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
	ck_http_connection_reader_init(&reader);
	assert(send(sockets[0], header, sizeof(header) - 1U, 0)
			== (ssize_t)(sizeof(header) - 1U));
	assert(shutdown(sockets[0], SHUT_WR) == 0);
	result = ck_http_connection_reader_drive(&reader, sockets[1],
			capture_body, &capture);
	assert(result == CK_HTTP_CONNECTION_READ_EOF_INCOMPLETE);
	assert(capture.length == 0);

	assert(close(sockets[0]) == 0);
	assert(close(sockets[1]) == 0);
}

static void test_body_queue_backpressure(void)
{
	static const char header[] =
		"POST /queue HTTP/1.1\r\n"
		"Host: x\r\n"
		"Content-Length: 100000\r\n"
		"\r\n";
	static unsigned char source[100000];
	unsigned char received[100000];
	ck_http_connection_reader_t reader;
	ck_http_request_body_t body_queue;
	body_sender_t sender;
	pthread_t sender_thread;
	int sockets[2];
	ck_http_connection_read_result_t result;
	size_t received_total = 0;
	size_t read;
	size_t blocked_available = 0;
	int observed_backpressure = 0;

	for (size_t i = 0; i < sizeof(source); i++)
	{
		source[i] = (unsigned char)(i % 251U);
	}
	memset(received, 0, sizeof(received));

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
	ck_http_connection_reader_init(&reader);
	ck_http_request_body_init(&body_queue);
	ck_http_connection_reader_attach_body_queue(&reader, &body_queue);

	send_all(sockets[0],
			(const unsigned char *)header, sizeof(header) - 1U);
	sender.socket_fd = sockets[0];
	sender.data = source;
	sender.length = sizeof(source);
	assert(pthread_create(&sender_thread, NULL, send_body_thread, &sender) == 0);

	while (!observed_backpressure)
	{
		result = ck_http_connection_reader_drive(
				&reader, sockets[1], NULL, NULL);
		assert(result == CK_HTTP_CONNECTION_READ_INCOMPLETE
				|| result == CK_HTTP_CONNECTION_READ_BODY_BACKPRESSURE
				|| result == CK_HTTP_CONNECTION_READ_REQUEST_COMPLETE);
		if (result == CK_HTTP_CONNECTION_READ_BODY_BACKPRESSURE)
		{
			observed_backpressure = 1;
			blocked_available = ck_http_request_body_available(&body_queue);
			assert(blocked_available > 0);
			assert(blocked_available <= CK_HTTP_REQUEST_BODY_BUFFER_BYTES);
			assert(ck_http_connection_reader_buffered_bytes(&reader) > 0);
		}
		else if (ck_http_request_body_is_finished(&body_queue))
		{
			assert(0 && "body queue completed before backpressure was observed");
		}
		sched_yield();
	}

	assert(ck_http_request_body_read(
			&body_queue, received, 32768U, &read)
			== CK_HTTP_REQUEST_BODY_READ_DATA);
	assert(read == 32768U);
	received_total += read;
	assert(ck_http_request_body_available(&body_queue) < blocked_available);

	while (!ck_http_request_body_is_finished(&body_queue))
	{
		result = ck_http_connection_reader_drive(
				&reader, sockets[1], NULL, NULL);
		assert(result == CK_HTTP_CONNECTION_READ_INCOMPLETE
			|| result == CK_HTTP_CONNECTION_READ_REQUEST_COMPLETE
			|| result == CK_HTTP_CONNECTION_READ_BODY_BACKPRESSURE);
		if (ck_http_request_body_available(&body_queue) != 0)
		{
			assert(ck_http_request_body_read(
					&body_queue,
					received + received_total,
					sizeof(received) - received_total,
					&read)
					== CK_HTTP_REQUEST_BODY_READ_DATA);
			received_total += read;
		}
		else if (result == CK_HTTP_CONNECTION_READ_INCOMPLETE)
		{
			sched_yield();
		}
	}

	assert(pthread_join(sender_thread, NULL) == 0);
	assert(received_total == sizeof(source));
	assert(memcmp(received, source, sizeof(source)) == 0);
	assert(close(sockets[0]) == 0);
	assert(close(sockets[1]) == 0);
}

int main(void)
{
	test_content_length_pipeline();
	test_body_sink_retry_does_not_replay();
	test_chunked_stream();
	test_chunked_split_crlf();
	test_read_batch_budget();
	test_eof_incomplete_body();
	test_body_queue_backpressure();
	return 0;
}
