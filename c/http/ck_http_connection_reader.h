#ifndef CKARTA_HTTP_CONNECTION_READER_H
#define CKARTA_HTTP_CONNECTION_READER_H

#include <stddef.h>

#include "ck_http_input.h"
#include "ck_http_request_body.h"

#define CK_HTTP_CONNECTION_READ_BUFFER_BYTES 65536u
#define CK_HTTP_CONNECTION_READ_BUDGET_BYTES 32768u
#define CK_HTTP_CONNECTION_PROCESS_BUDGET_BYTES 32768u

typedef enum ck_http_connection_read_result
{
	CK_HTTP_CONNECTION_READ_INCOMPLETE = 0,
	CK_HTTP_CONNECTION_READ_REQUEST_COMPLETE = 1,
	CK_HTTP_CONNECTION_READ_BAD_REQUEST = 2,
	CK_HTTP_CONNECTION_READ_TOO_LARGE = 3,
	CK_HTTP_CONNECTION_READ_EOF_INCOMPLETE = 4,
	CK_HTTP_CONNECTION_READ_IO_ERROR = 5,
	CK_HTTP_CONNECTION_READ_SINK_ERROR = 6,
	CK_HTTP_CONNECTION_READ_BODY_BACKPRESSURE = 7
} ck_http_connection_read_result_t;


#define CK_HTTP_BODY_SINK_WOULD_BLOCK 2

typedef int (*ck_http_body_sink_fn)(
	void *context,
	const unsigned char *data,
	size_t length);

typedef struct ck_http_connection_reader
{
	ck_http_input_t input;
	ck_http_request_body_t *body_queue;
	unsigned char buffer[CK_HTTP_CONNECTION_READ_BUFFER_BYTES];
	size_t begin;
	size_t end;
} ck_http_connection_reader_t;

void ck_http_connection_reader_init(ck_http_connection_reader_t *reader);
void ck_http_connection_reader_attach_body_queue(
	ck_http_connection_reader_t *reader,
	ck_http_request_body_t *body_queue);
ck_http_connection_read_result_t ck_http_connection_reader_drive(
	ck_http_connection_reader_t *reader,
	int socket_fd,
	ck_http_body_sink_fn body_sink,
	void *body_sink_context);
int ck_http_connection_reader_next_request(
	ck_http_connection_reader_t *reader);
const ck_http_request_t *ck_http_connection_reader_request(
	const ck_http_connection_reader_t *reader);
size_t ck_http_connection_reader_buffered_bytes(
	const ck_http_connection_reader_t *reader);

#endif
