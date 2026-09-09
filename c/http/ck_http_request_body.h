#ifndef CKARTA_HTTP_REQUEST_BODY_H
#define CKARTA_HTTP_REQUEST_BODY_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#define CK_HTTP_REQUEST_BODY_BUFFER_BYTES 65536u

typedef enum ck_http_request_body_write_result
{
	CK_HTTP_REQUEST_BODY_WRITE_OK = 0,
	CK_HTTP_REQUEST_BODY_WRITE_WOULD_BLOCK = 1,
	CK_HTTP_REQUEST_BODY_WRITE_CLOSED = 2,
	CK_HTTP_REQUEST_BODY_WRITE_INVALID = -1
} ck_http_request_body_write_result_t;

typedef enum ck_http_request_body_read_result
{
	CK_HTTP_REQUEST_BODY_READ_DATA = 0,
	CK_HTTP_REQUEST_BODY_READ_WOULD_BLOCK = 1,
	CK_HTTP_REQUEST_BODY_READ_EOF = 2,
	CK_HTTP_REQUEST_BODY_READ_ERROR = 3,
	CK_HTTP_REQUEST_BODY_READ_INVALID = -1
} ck_http_request_body_read_result_t;

/*
 * Single-producer/single-consumer bounded byte FIFO.
 *
 * Producer owns write operations.
 * Consumer owns read operations.
 * Neither side may perform the opposite operation.
 */
typedef struct ck_http_request_body
{
	_Atomic uint64_t head;
	_Atomic uint64_t tail;
	_Atomic int32_t eof;
	_Atomic int32_t error;
	unsigned char buffer[CK_HTTP_REQUEST_BODY_BUFFER_BYTES];
} ck_http_request_body_t;

void ck_http_request_body_init(ck_http_request_body_t *body);
ck_http_request_body_write_result_t ck_http_request_body_write(
	ck_http_request_body_t *body,
	const unsigned char *data,
	size_t length,
	size_t *written);
ck_http_request_body_read_result_t ck_http_request_body_read(
	ck_http_request_body_t *body,
	unsigned char *data,
	size_t capacity,
	size_t *read);
void ck_http_request_body_mark_eof(ck_http_request_body_t *body);
void ck_http_request_body_mark_error(ck_http_request_body_t *body);
size_t ck_http_request_body_available(const ck_http_request_body_t *body);
int ck_http_request_body_is_finished(const ck_http_request_body_t *body);

#endif
