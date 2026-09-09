#include "../../c/output/ck_http_response.h"

#include <assert.h>
#include <string.h>

static void test_response_lifecycle(void)
{
	static const unsigned char body[] = "hello";
	ck_http_response_t response;

	ck_http_response_init(&response);
	assert(ck_http_response_state(&response) == CK_HTTP_RESPONSE_NEW);
	assert(ck_http_response_set_status(&response, 201U) == 0);
	assert(ck_http_response_set_content_length(&response, 5U) == 0);
	assert(ck_http_response_set_connection_close(&response, 1) == 0);
	assert(ck_http_response_write_body(&response, body, sizeof(body) - 1U) == 0);
	assert(ck_http_response_state(&response) == CK_HTTP_RESPONSE_COMMITTED);
	assert(ck_http_response_body_length(&response) == 5U);
	assert(memcmp(ck_http_response_body(&response), body, 5U) == 0);
	assert(ck_http_response_set_status(&response, 202U) == -1);
	assert(ck_http_response_reset(&response) == -1);
	assert(ck_http_response_finish(&response) == 0);
	assert(ck_http_response_state(&response) == CK_HTTP_RESPONSE_FINISHED);
	assert(ck_http_response_finish(&response) == -1);
}

static void test_reset_before_commit(void)
{
	ck_http_response_t response;

	ck_http_response_init(&response);
	assert(ck_http_response_set_status(&response, 404U) == 0);
	assert(ck_http_response_set_content_length(&response, 0U) == 0);
	assert(ck_http_response_reset(&response) == 0);
	assert(ck_http_response_state(&response) == CK_HTTP_RESPONSE_NEW);
	assert(ck_http_response_set_status(&response, 204U) == 0);
	assert(ck_http_response_commit(&response) == 0);
	assert(ck_http_response_finish(&response) == 0);
}

static void test_content_length_mismatch(void)
{
	static const unsigned char body[] = "hi";
	ck_http_response_t response;

	ck_http_response_init(&response);
	assert(ck_http_response_set_content_length(&response, 3U) == 0);
	assert(ck_http_response_write_body(&response, body, 2U) == 0);
	assert(ck_http_response_finish(&response) == -2);
	assert(ck_http_response_state(&response) == CK_HTTP_RESPONSE_FAILED);
	assert(ck_http_response_write_body(&response, body, 1U) == -1);
}

static void test_body_buffer_bound(void)
{
	static unsigned char body[CK_HTTP_RESPONSE_BODY_BUFFER_BYTES];
	static const unsigned char extra = 'x';
	ck_http_response_t response;

	memset(body, 'z', sizeof(body));
	ck_http_response_init(&response);
	assert(ck_http_response_write_body(&response, body, sizeof(body)) == 0);
	assert(ck_http_response_body_length(&response) == sizeof(body));
	assert(ck_http_response_write_body(&response, &extra, 1U) == -2);
	assert(ck_http_response_state(&response) == CK_HTTP_RESPONSE_FAILED);
}

static void test_invalid_status(void)
{
	ck_http_response_t response;

	ck_http_response_init(&response);
	assert(ck_http_response_set_status(&response, 100U) == -1);
	assert(ck_http_response_set_status(&response, 600U) == -1);
	assert(ck_http_response_commit(&response) == 0);
	assert(ck_http_response_state(&response) == CK_HTTP_RESPONSE_COMMITTED);
}

int main(void)
{
	test_response_lifecycle();
	test_reset_before_commit();
	test_content_length_mismatch();
	test_body_buffer_bound();
	test_invalid_status();
	return 0;
}
