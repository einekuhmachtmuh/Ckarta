#include "../../c/http/ck_http_parser.h"

#include <assert.h>
#include <string.h>

static ck_http_parse_result_t parse_split(const char *request,
	ck_http_parser_t *parser, ck_http_request_t *parsed, size_t *consumed)
{
	ck_http_parse_result_t result;
	size_t first = 0;
	size_t second = 0;
	size_t split = strlen(request) / 2;

	result = ck_http_parser_feed(parser, request, split, &first, parsed);
	assert(result == CK_HTTP_PARSE_INCOMPLETE);
	assert(first == split);

	result = ck_http_parser_feed(parser, request + split,
		strlen(request) - split, &second, parsed);
	if (result == CK_HTTP_PARSE_COMPLETE || result == CK_HTTP_PARSE_BAD_REQUEST)
	{
		assert(second <= strlen(request) - split);
		*consumed = split + second;
	}
	else
	{
		*consumed = first + second;
	}
	return result;
}

static void test_valid_content_length(void)
{
	const char request[] =
		"POST /submit HTTP/1.1\r\n"
		"Host: localhost\r\n"
		"Content-Length: 5\r\n"
		"X-Test: value\r\n\r\n"
		"helloREST";
	ck_http_parser_t parser;
	ck_http_request_t parsed;
	size_t consumed;

	ck_http_parser_init(&parser);
	assert(parse_split(request, &parser, &parsed, &consumed) == CK_HTTP_PARSE_COMPLETE);
	assert(consumed == (size_t)(strstr(request, "helloREST") - request));
	assert(parsed.method.length == 4 && memcmp(parsed.method.data, "POST", 4) == 0);
	assert(parsed.target.length == 7 && memcmp(parsed.target.data, "/submit", 7) == 0);
	assert(parsed.header_count == 3);
	assert(parsed.body_mode == CK_HTTP_BODY_CONTENT_LENGTH);
	assert(parsed.content_length == 5);
	assert(parsed.connection_close_required == 0);
}

static void test_duplicate_equal_content_length(void)
{
	const char request[] =
		"POST / HTTP/1.1\r\n"
		"Host: localhost\r\n"
		"Content-Length: 5\r\n"
		"Content-Length: 5\r\n\r\n";
	ck_http_parser_t parser;
	ck_http_request_t parsed;
	size_t consumed;

	ck_http_parser_init(&parser);
	assert(parse_split(request, &parser, &parsed, &consumed) == CK_HTTP_PARSE_COMPLETE);
	assert(consumed == strlen(request));
	assert(parsed.content_length == 5);
}

static void test_conflicting_content_length(void)
{
	const char request[] =
		"POST / HTTP/1.1\r\n"
		"Content-Length: 5, 6\r\n\r\n";
	ck_http_parser_t parser;
	ck_http_request_t parsed;
	size_t consumed;

	ck_http_parser_init(&parser);
	assert(parse_split(request, &parser, &parsed, &consumed) == CK_HTTP_PARSE_BAD_REQUEST);
}

static void test_chunked_overrides_content_length(void)
{
	const char request[] =
		"POST / HTTP/1.1\r\n"
		"Content-Length: 5\r\n"
		"Transfer-Encoding: chunked\r\n\r\n";
	ck_http_parser_t parser;
	ck_http_request_t parsed;
	size_t consumed;

	ck_http_parser_init(&parser);
	assert(parse_split(request, &parser, &parsed, &consumed) == CK_HTTP_PARSE_COMPLETE);
	assert(parsed.body_mode == CK_HTTP_BODY_CHUNKED);
	assert(parsed.content_length == 0);
	assert(parsed.connection_close_required == 1);
}

static void test_non_chunked_final_transfer_encoding_is_rejected(void)
{
	const char request[] =
		"POST / HTTP/1.1\r\n"
		"Transfer-Encoding: gzip\r\n\r\n";
	ck_http_parser_t parser;
	ck_http_request_t parsed;
	size_t consumed;
	ck_http_parse_result_t result;

	ck_http_parser_init(&parser);
	result = parse_split(request, &parser, &parsed, &consumed);
	assert(result == CK_HTTP_PARSE_BAD_REQUEST);
}

static void test_bad_request_line(void)
{
	const char request[] = "GE(T / HTTP/1.1\r\nHost: localhost\r\n\r\n";
	ck_http_parser_t parser;
	ck_http_request_t parsed;
	size_t consumed;

	ck_http_parser_init(&parser);
	assert(parse_split(request, &parser, &parsed, &consumed) == CK_HTTP_PARSE_BAD_REQUEST);
}

static void test_header_too_large(void)
{
	char request[CK_HTTP_MAX_HEADER_BYTES + 1];
	ck_http_parser_t parser;
	ck_http_request_t parsed;
	size_t consumed;

	memset(request, 'A', CK_HTTP_MAX_HEADER_BYTES);
	request[CK_HTTP_MAX_HEADER_BYTES] = '\0';
	ck_http_parser_init(&parser);
	assert(ck_http_parser_feed(&parser, request, CK_HTTP_MAX_HEADER_BYTES,
		&consumed, &parsed) == CK_HTTP_PARSE_INCOMPLETE);
	assert(consumed == CK_HTTP_MAX_HEADER_BYTES);
	assert(ck_http_parser_feed(&parser, "X", 1, &consumed, &parsed)
			== CK_HTTP_PARSE_HEADER_TOO_LARGE);
}

int main(void)
{
	test_valid_content_length();
	test_duplicate_equal_content_length();
	test_conflicting_content_length();
	test_chunked_overrides_content_length();
	test_non_chunked_final_transfer_encoding_is_rejected();
	test_bad_request_line();
	test_header_too_large();
	return 0;
}
