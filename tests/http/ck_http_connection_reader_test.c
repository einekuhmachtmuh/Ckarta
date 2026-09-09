#define _GNU_SOURCE

#include "../../c/http/ck_http_connection_reader.h"

#include <assert.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct body_capture
{
	unsigned char data[64];
	size_t length;
} body_capture_t;

static int capture_body(void *context, const unsigned char *data, size_t length)
{
	body_capture_t *capture = context;

	assert(capture != NULL);
	assert(capture->length + length <= sizeof(capture->data));
	memcpy(capture->data + capture->length, data, length);
	capture->length += length;
	return 0;
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

	ck_http_connection_reader_next_request(&reader);
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

int main(void)
{
	test_content_length_pipeline();
	test_chunked_stream();
	test_chunked_split_crlf();
	test_eof_incomplete_body();
	return 0;
}