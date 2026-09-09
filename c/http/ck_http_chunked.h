#ifndef CKARTA_HTTP_CHUNKED_H
#define CKARTA_HTTP_CHUNKED_H

#include <stddef.h>
#include <stdint.h>

#define CK_HTTP_CHUNKED_MAX_LINE 8192u
#define CK_HTTP_CHUNKED_MAX_TRAILERS 64u
#define CK_HTTP_CHUNKED_MAX_TRAILER_BYTES 16384u
#define CK_HTTP_CHUNKED_MAX_BODY UINT64_MAX

typedef enum ck_http_chunked_result
{
	CK_HTTP_CHUNKED_INCOMPLETE = 0,
	CK_HTTP_CHUNKED_DATA = 1,
	CK_HTTP_CHUNKED_COMPLETE = 2,
	CK_HTTP_CHUNKED_BAD_REQUEST = 3,
	CK_HTTP_CHUNKED_TOO_LARGE = 4
} ck_http_chunked_result_t;

typedef enum ck_http_chunked_state
{
	CK_HTTP_CHUNKED_SIZE = 0,
	CK_HTTP_CHUNKED_DATA_STATE = 1,
	CK_HTTP_CHUNKED_DATA_CRLF = 2,
	CK_HTTP_CHUNKED_TRAILERS = 3,
	CK_HTTP_CHUNKED_DONE = 4,
	CK_HTTP_CHUNKED_STATE_INVALID = -1
} ck_http_chunked_state_t;

typedef struct ck_http_chunked_decoder
{
	ck_http_chunked_state_t state;
	uint64_t chunk_remaining;
	uint64_t total_decoded;
	size_t line_length;
	size_t trailer_bytes;
	size_t trailer_count;
	char line[CK_HTTP_CHUNKED_MAX_LINE];
} ck_http_chunked_decoder_t;

void ck_http_chunked_init(ck_http_chunked_decoder_t *decoder);
ck_http_chunked_result_t ck_http_chunked_feed(
	ck_http_chunked_decoder_t *decoder,
	const void *data,
	size_t length,
	size_t *consumed,
	const unsigned char **body_data,
	size_t *body_length);

ck_http_chunked_state_t ck_http_chunked_state(
	const ck_http_chunked_decoder_t *decoder);
uint64_t ck_http_chunked_total_decoded(
	const ck_http_chunked_decoder_t *decoder);

#endif
