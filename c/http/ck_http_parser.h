#ifndef CKARTA_HTTP_PARSER_H
#define CKARTA_HTTP_PARSER_H

#include <stddef.h>
#include <stdint.h>

#define CK_HTTP_MAX_HEADERS 128u
#define CK_HTTP_MAX_HEADER_BYTES 32768u
#define CK_HTTP_MAX_METHOD_BYTES 32u
#define CK_HTTP_MAX_TARGET_BYTES 8192u
#define CK_HTTP_MAX_VERSION_BYTES 16u
#define CK_HTTP_MAX_HEADER_NAME_BYTES 256u
#define CK_HTTP_MAX_HEADER_VALUE_BYTES 16384u
#define CK_HTTP_MAX_BODY_LENGTH UINT64_MAX

typedef enum ck_http_parse_result
{
	CK_HTTP_PARSE_INCOMPLETE = 0,
	CK_HTTP_PARSE_COMPLETE = 1,
	CK_HTTP_PARSE_BAD_REQUEST = 2,
	CK_HTTP_PARSE_HEADER_TOO_LARGE = 3
} ck_http_parse_result_t;

typedef enum ck_http_body_mode
{
	CK_HTTP_BODY_NONE = 0,
	CK_HTTP_BODY_CONTENT_LENGTH = 1,
	CK_HTTP_BODY_CHUNKED = 2
} ck_http_body_mode_t;

typedef struct ck_http_span
{
	const char *data;
	size_t length;
} ck_http_span_t;

typedef struct ck_http_header
{
	ck_http_span_t name;
	ck_http_span_t value;
} ck_http_header_t;

typedef struct ck_http_request
{
	ck_http_span_t method;
	ck_http_span_t target;
	ck_http_span_t version;
	ck_http_header_t headers[CK_HTTP_MAX_HEADERS];
	size_t header_count;
	ck_http_body_mode_t body_mode;
	uint64_t content_length;
	int connection_close_required;
} ck_http_request_t;

typedef struct ck_http_parser
{
	char buffer[CK_HTTP_MAX_HEADER_BYTES];
	size_t length;
	int complete;
} ck_http_parser_t;

void ck_http_parser_init(ck_http_parser_t *parser);
ck_http_parse_result_t ck_http_parser_feed(
	ck_http_parser_t *parser,
	const void *data,
	size_t length,
	size_t *consumed,
	ck_http_request_t *request);

#endif
