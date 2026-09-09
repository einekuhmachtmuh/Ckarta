#include <assert.h>
#include <string.h>

#include "../../c/http/ck_http_input.h"

static void test_content_length_body_and_pipeline(void)
{
	static const char request_and_body[] =
		"POST /upload HTTP/1.1\r\n"
		"Host: example.test\r\n"
		"Content-Length: 5\r\n"
		"\r\n"
		"hello"
		"GET /next HTTP/1.1\r\n"
		"Host: example.test\r\n"
		"\r\n";
	ck_http_input_t input;
	const unsigned char *body;
	size_t body_length;
	size_t consumed;
	ck_http_input_result_t result;
	const char *next_request;
	const char next_request_text[] =
		"GET /next HTTP/1.1\r\nHost: example.test\r\n\r\n";
	size_t header_length = strlen("POST /upload HTTP/1.1\r\nHost: example.test\r\nContent-Length: 5\r\n\r\n");

	ck_http_input_init(&input);
	result = ck_http_input_feed(&input, request_and_body,
			header_length + 5 + strlen(next_request_text), &consumed,
			&body, &body_length);
	assert(result == CK_HTTP_INPUT_COMPLETE);
	assert(consumed == header_length + 5);
	assert(body_length == 5);
	assert(memcmp(body, "hello", 5) == 0);
	assert(ck_http_input_complete(&input));
	assert(input.request.method.length == 4);
	assert(memcmp(input.request.method.data, "POST", 4) == 0);
	assert(input.request.body_mode == CK_HTTP_BODY_CONTENT_LENGTH);

	next_request = request_and_body + consumed;
	ck_http_input_next_request(&input);
	result = ck_http_input_feed(&input, next_request, strlen(next_request),
			&consumed, &body, &body_length);
	assert(result == CK_HTTP_INPUT_COMPLETE);
	assert(consumed == strlen(next_request_text));
	assert(body_length == 0);
	assert(input.request.target.length == 5);
	assert(memcmp(input.request.target.data, "/next", 5) == 0);
}

static void test_content_length_fragmented_and_eof(void)
{
	static const char header[] =
		"POST /x HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\n";
	static const char body_part[] = "he";
	static const char body_rest[] = "llo";
	ck_http_input_t input;
	const unsigned char *body;
	size_t body_length;
	size_t consumed;
	ck_http_input_result_t result;

	ck_http_input_init(&input);
	result = ck_http_input_feed(&input, header, strlen(header),
			&consumed, &body, &body_length);
	assert(result == CK_HTTP_INPUT_INCOMPLETE);
	assert(consumed == strlen(header));
	assert(body_length == 0);

	result = ck_http_input_feed(&input, body_part, strlen(body_part),
			&consumed, &body, &body_length);
	assert(result == CK_HTTP_INPUT_BODY);
	assert(consumed == 2);
	assert(body_length == 2);
	assert(memcmp(body, "he", 2) == 0);

	result = ck_http_input_feed(&input, body_rest, strlen(body_rest),
			&consumed, &body, &body_length);
	assert(result == CK_HTTP_INPUT_COMPLETE);
	assert(consumed == 3);
	assert(body_length == 3);
	assert(memcmp(body, "llo", 3) == 0);

	ck_http_input_next_request(&input);
	result = ck_http_input_eof(&input, &body, &body_length);
	assert(result == CK_HTTP_INPUT_EOF_INCOMPLETE);
}

static void test_chunked_body_and_pipeline(void)
{
	static const char input[] =
		"POST /chunked HTTP/1.1\r\n"
		"Transfer-Encoding: chunked\r\n"
		"\r\n"
		"4\r\nWiki\r\n"
		"5\r\npedia\r\n"
		"0\r\nX-Trailer: yes\r\n\r\n"
		"GET /next HTTP/1.1\r\nHost: x\r\n\r\n";
	ck_http_input_t state;
	const unsigned char *body;
	size_t body_length;
	size_t consumed;
	size_t offset = 0;
	size_t total_body = 0;
	ck_http_input_result_t result;

	ck_http_input_init(&state);
	while (offset < strlen(input))
	{
		size_t feed_length = strlen(input) - offset;
		if (feed_length > 3)
		{
			feed_length = 3;
		}
		result = ck_http_input_feed(&state, input + offset, feed_length,
				&consumed, &body, &body_length);
		assert(consumed <= feed_length);
		if (body_length != 0)
		{
			total_body += body_length;
		}
		offset += consumed;
		if (result == CK_HTTP_INPUT_COMPLETE)
		{
			break;
		}
		assert(result == CK_HTTP_INPUT_INCOMPLETE || result == CK_HTTP_INPUT_BODY);
		assert(consumed != 0 || feed_length == 0);
	}

	assert(ck_http_input_complete(&state));
	assert(total_body == 9);
	assert(offset < strlen(input));

	ck_http_input_next_request(&state);
	result = ck_http_input_feed(&state, input + offset, strlen(input) - offset,
			&consumed, &body, &body_length);
	assert(result == CK_HTTP_INPUT_COMPLETE);
	assert(consumed == strlen(input) - offset);
	assert(body_length == 0);
	assert(state.request.target.length == 5);
	assert(memcmp(state.request.target.data, "/next", 5) == 0);
}

static void test_zero_body_completes_at_header(void)
{
	static const char request[] =
		"GET / HTTP/1.1\r\nHost: x\r\n\r\nextra";
	ck_http_input_t input;
	const unsigned char *body;
	size_t body_length;
	size_t consumed;
	ck_http_input_result_t result;

	ck_http_input_init(&input);
	result = ck_http_input_feed(&input, request, strlen(request),
			&consumed, &body, &body_length);
	assert(result == CK_HTTP_INPUT_COMPLETE);
	assert(consumed == strlen("GET / HTTP/1.1\r\nHost: x\r\n\r\n"));
	assert(body_length == 0);
}

int main(void)
{
	test_content_length_body_and_pipeline();
	test_content_length_fragmented_and_eof();
	test_chunked_body_and_pipeline();
	test_zero_body_completes_at_header();
	return 0;
}
