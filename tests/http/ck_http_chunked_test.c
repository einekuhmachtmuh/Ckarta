#include "../../c/http/ck_http_chunked.h"

#include <assert.h>
#include <string.h>

static void feed_all_at_small_splits(const char *input, size_t length,
	ck_http_chunked_decoder_t *decoder)
{
	size_t offset = 0;
	unsigned char received[64] = {0};
	size_t received_length = 0;

	while (offset < length)
	{
		size_t feed_length = length - offset;
		size_t consumed;
		const unsigned char *body;
		size_t body_length;
		ck_http_chunked_result_t result;

		if (feed_length > 4)
		{
			feed_length = 4;
		}
		result = ck_http_chunked_feed(decoder, input + offset, feed_length,
			&consumed, &body, &body_length);
		assert(consumed <= feed_length);
		if (body_length != 0)
		{
			assert(body != NULL);
			assert(received_length + body_length <= sizeof(received));
			memcpy(received + received_length, body, body_length);
			received_length += body_length;
		}
		assert(result != CK_HTTP_CHUNKED_BAD_REQUEST);
		assert(result != CK_HTTP_CHUNKED_TOO_LARGE);
		if (consumed == 0)
		{
			assert(feed_length < length - offset);
			feed_length = length - offset;
			result = ck_http_chunked_feed(decoder, input + offset, feed_length,
				&consumed, &body, &body_length);
			assert(consumed > 0);
			assert(result != CK_HTTP_CHUNKED_BAD_REQUEST);
			assert(result != CK_HTTP_CHUNKED_TOO_LARGE);
			if (body_length != 0)
			{
				assert(body != NULL);
				assert(received_length + body_length <= sizeof(received));
				memcpy(received + received_length, body, body_length);
				received_length += body_length;
			}
		}
		offset += consumed;
	}

	if (ck_http_chunked_state(decoder) != CK_HTTP_CHUNKED_DONE)
	{
		size_t consumed;
		const unsigned char *body;
		size_t body_length;
		ck_http_chunked_result_t result = ck_http_chunked_feed(
				decoder, NULL, 0, &consumed, &body, &body_length);
		assert(consumed == 0);
		assert(body == NULL);
		assert(body_length == 0);
		assert(result == CK_HTTP_CHUNKED_INCOMPLETE);
	}

	assert(ck_http_chunked_state(decoder) == CK_HTTP_CHUNKED_DONE);
	assert(received_length == strlen("Wikipedia"));
	assert(memcmp(received, "Wikipedia", received_length) == 0);
	assert(ck_http_chunked_total_decoded(decoder) == received_length);
}

static void test_incremental_chunked_body(void)
{
	const char input[] =
		"4\r\nWiki\r\n"
		"5;foo=bar\r\npedia\r\n"
		"0\r\n"
		"X-Trailer: yes\r\n"
		"\r\n";
	ck_http_chunked_decoder_t decoder;

	ck_http_chunked_init(&decoder);
	feed_all_at_small_splits(input, strlen(input), &decoder);
}

static void test_chunk_size_overflow(void)
{
	const char input[] = "FFFFFFFFFFFFFFFFF\r\n";
	ck_http_chunked_decoder_t decoder;
	size_t consumed;
	const unsigned char *body;
	size_t body_length;

	ck_http_chunked_init(&decoder);
	assert(ck_http_chunked_feed(&decoder, input, strlen(input), &consumed,
		&body, &body_length) == CK_HTTP_CHUNKED_BAD_REQUEST);
}

static void test_missing_data_crlf_rejected(void)
{
	const char input[] = "1\n";
	ck_http_chunked_decoder_t decoder;
	size_t consumed;
	const unsigned char *body;
	size_t body_length;

	ck_http_chunked_init(&decoder);
	assert(ck_http_chunked_feed(&decoder, input, strlen(input), &consumed,
		&body, &body_length) == CK_HTTP_CHUNKED_BAD_REQUEST);
}

static void test_truncated_chunk_data(void)
{
	const char input[] = "5\r\nabc";
	ck_http_chunked_decoder_t decoder;
	size_t consumed;
	const unsigned char *body;
	size_t body_length;

	ck_http_chunked_init(&decoder);
	assert(ck_http_chunked_feed(&decoder, input, strlen(input), &consumed,
		&body, &body_length) == CK_HTTP_CHUNKED_DATA);
	assert(body_length == 3);
	assert(ck_http_chunked_state(&decoder) == CK_HTTP_CHUNKED_DATA_STATE);
}

int main(void)
{
	test_incremental_chunked_body();
	test_chunk_size_overflow();
	test_missing_data_crlf_rejected();
	test_truncated_chunk_data();
	return 0;
}
