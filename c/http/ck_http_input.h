#ifndef CKARTA_HTTP_INPUT_H
#define CKARTA_HTTP_INPUT_H

#include <stddef.h>
#include <stdint.h>

#include "ck_http_chunked.h"
#include "ck_http_parser.h"

typedef enum ck_http_input_result
{
	CK_HTTP_INPUT_INCOMPLETE = 0,
	CK_HTTP_INPUT_BODY = 1,
	CK_HTTP_INPUT_COMPLETE = 2,
	CK_HTTP_INPUT_BAD_REQUEST = 3,
	CK_HTTP_INPUT_TOO_LARGE = 4,
	CK_HTTP_INPUT_EOF_INCOMPLETE = 5
} ck_http_input_result_t;

typedef struct ck_http_input
{
	ck_http_parser_t parser;
	ck_http_chunked_decoder_t chunked;
	ck_http_request_t request;
	uint64_t content_length_remaining;
	int header_complete;
	int message_complete;
	const unsigned char *pending_body_data;
	size_t pending_body_length;
	size_t pending_input_consumed;
	int pending_message_complete;
} ck_http_input_t;

void ck_http_input_init(ck_http_input_t *input);
ck_http_input_result_t ck_http_input_feed(
	ck_http_input_t *input,
	const void *data,
	size_t length,
	size_t *consumed,
	const unsigned char **body_data,
	size_t *body_length);
int ck_http_input_ack_body(
	ck_http_input_t *input,
	size_t *consumed);
ck_http_input_result_t ck_http_input_eof(
	ck_http_input_t *input,
	const unsigned char **body_data,
	size_t *body_length);
const ck_http_request_t *ck_http_input_request(const ck_http_input_t *input);
int ck_http_input_complete(const ck_http_input_t *input);
void ck_http_input_next_request(ck_http_input_t *input);

#endif
