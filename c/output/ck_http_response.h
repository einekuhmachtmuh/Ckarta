#ifndef CKARTA_HTTP_RESPONSE_H
#define CKARTA_HTTP_RESPONSE_H

#include <stddef.h>
#include <stdint.h>

#define CK_HTTP_RESPONSE_BODY_BUFFER_BYTES 65536u

typedef enum ck_http_response_state
{
	CK_HTTP_RESPONSE_NEW = 0,
	CK_HTTP_RESPONSE_COMMITTED = 1,
	CK_HTTP_RESPONSE_FINISHED = 2,
	CK_HTTP_RESPONSE_FAILED = 3
} ck_http_response_state_t;

typedef struct ck_http_response
{
	ck_http_response_state_t state;
	uint16_t status;
	uint64_t content_length;
	int content_length_set;
	int connection_close;
	unsigned char body[CK_HTTP_RESPONSE_BODY_BUFFER_BYTES];
	size_t body_length;
} ck_http_response_t;

void ck_http_response_init(ck_http_response_t *response);
ck_http_response_state_t ck_http_response_state(
		const ck_http_response_t *response);
int ck_http_response_set_status(ck_http_response_t *response,
		uint16_t status);
int ck_http_response_set_content_length(ck_http_response_t *response,
		uint64_t content_length);
int ck_http_response_set_connection_close(ck_http_response_t *response,
		int enabled);
int ck_http_response_reset(ck_http_response_t *response);
int ck_http_response_write_body(ck_http_response_t *response,
		const unsigned char *data,
		size_t length);
int ck_http_response_commit(ck_http_response_t *response);
int ck_http_response_finish(ck_http_response_t *response);
const unsigned char *ck_http_response_body(
		const ck_http_response_t *response);
size_t ck_http_response_body_length(
		const ck_http_response_t *response);

#endif
