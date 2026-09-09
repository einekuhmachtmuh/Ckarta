#ifndef CKARTA_HTTP_OUTPUT_WRITER_H
#define CKARTA_HTTP_OUTPUT_WRITER_H

#include <stddef.h>

#define CK_HTTP_OUTPUT_WRITE_BUFFER_BYTES 65536u
#define CK_HTTP_OUTPUT_WRITE_BUDGET_BYTES 32768u

typedef enum ck_http_output_write_result
{
	CK_HTTP_OUTPUT_WRITE_DRAINED = 0,
	CK_HTTP_OUTPUT_WRITE_NEED_WRITE = 1,
	CK_HTTP_OUTPUT_WRITE_PEER_CLOSED = 2,
	CK_HTTP_OUTPUT_WRITE_IO_ERROR = 3
} ck_http_output_write_result_t;

typedef struct ck_http_output_writer
{
	int socket_fd;
	unsigned char buffer[CK_HTTP_OUTPUT_WRITE_BUFFER_BYTES];
	size_t begin;
	size_t end;
} ck_http_output_writer_t;

void ck_http_output_writer_init(ck_http_output_writer_t *writer);
int ck_http_output_writer_attach_socket(
	ck_http_output_writer_t *writer,
	int socket_fd);
int ck_http_output_writer_queue(
	ck_http_output_writer_t *writer,
	const unsigned char *data,
	size_t length);
ck_http_output_write_result_t ck_http_output_writer_drive(
	ck_http_output_writer_t *writer);
size_t ck_http_output_writer_buffered_bytes(
	const ck_http_output_writer_t *writer);

#endif
