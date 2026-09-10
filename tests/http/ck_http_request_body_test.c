#include "../../c/http/ck_http_request_body.h"

#include <assert.h>
#include <string.h>

int main(void)
{
	ck_http_request_body_t body;
	unsigned char source[CK_HTTP_REQUEST_BODY_BUFFER_BYTES + 32u];
	unsigned char received[CK_HTTP_REQUEST_BODY_BUFFER_BYTES + 32u];
	size_t written;
	size_t read;
	size_t first;

	for (size_t i = 0; i < sizeof(source); i++)
	{
		source[i] = (unsigned char)(i & 0xffu);
	}
	memset(received, 0, sizeof(received));

	ck_http_request_body_init(&body);
	assert(ck_http_request_body_available(&body) == 0);
	assert(!ck_http_request_body_is_finished(&body));

	assert(ck_http_request_body_write(
			&body, source, CK_HTTP_REQUEST_BODY_BUFFER_BYTES,
			&written) == CK_HTTP_REQUEST_BODY_WRITE_OK);
	assert(written == CK_HTTP_REQUEST_BODY_BUFFER_BYTES);
	assert(ck_http_request_body_write(
			&body, source, 1, &written)
			== CK_HTTP_REQUEST_BODY_WRITE_WOULD_BLOCK);
	assert(written == 0);

	assert(ck_http_request_body_read(
			&body, received, 12345u, &read)
			== CK_HTTP_REQUEST_BODY_READ_DATA);
	assert(read == 12345u);

	first = CK_HTTP_REQUEST_BODY_BUFFER_BYTES - 12345u;
	assert(ck_http_request_body_write(
			&body, source, 12345u, &written)
			== CK_HTTP_REQUEST_BODY_WRITE_OK);
	assert(written == 12345u);

	assert(ck_http_request_body_read(
			&body, received + 12345u,
			CK_HTTP_REQUEST_BODY_BUFFER_BYTES,
			&read)
			== CK_HTTP_REQUEST_BODY_READ_DATA);
	assert(read == CK_HTTP_REQUEST_BODY_BUFFER_BYTES);
	assert(memcmp(received, source, 12345u) == 0);
	assert(memcmp(received + 12345u,
		source + 12345u, first) == 0);
	assert(memcmp(received + 12345u + first,
		source, 12345u) == 0);

	assert(ck_http_request_body_available(&body) == 0);
	assert(ck_http_request_body_write(
			&body, source + first, 32u, &written)
			== CK_HTTP_REQUEST_BODY_WRITE_OK);
	assert(written == 32u);
	assert(ck_http_request_body_read(
			&body, received, 32u, &read)
			== CK_HTTP_REQUEST_BODY_READ_DATA);
	assert(read == 32u);
	assert(memcmp(received, source + first, 32u) == 0);

	ck_http_request_body_mark_eof(&body);
	assert(ck_http_request_body_is_finished(&body));
	assert(ck_http_request_body_read(
			&body, received, sizeof(received), &read)
			== CK_HTTP_REQUEST_BODY_READ_EOF);
	assert(ck_http_request_body_write(
			&body, source, 1, &written)
			== CK_HTTP_REQUEST_BODY_WRITE_CLOSED);

	ck_http_request_body_init(&body);
	assert(ck_http_request_body_write(
			&body, source, CK_HTTP_REQUEST_BODY_BUFFER_BYTES - 1u,
			&written) == CK_HTTP_REQUEST_BODY_WRITE_OK);
	assert(written == CK_HTTP_REQUEST_BODY_BUFFER_BYTES - 1u);
	assert(ck_http_request_body_write(
			&body, source + 16u, 2u, &written)
			== CK_HTTP_REQUEST_BODY_WRITE_WOULD_BLOCK);
	assert(written == 0);
	assert(ck_http_request_body_available(&body)
			== CK_HTTP_REQUEST_BODY_BUFFER_BYTES - 1u);
	assert(ck_http_request_body_read(
			&body, received, 1u, &read)
			== CK_HTTP_REQUEST_BODY_READ_DATA);
	assert(read == 1u);
	assert(ck_http_request_body_write(
			&body, source + 16u, 2u, &written)
			== CK_HTTP_REQUEST_BODY_WRITE_OK);
	assert(written == 2u);

	ck_http_request_body_mark_eof(&body);
	assert(ck_http_request_body_read(
			&body, received, sizeof(received), &read)
			== CK_HTTP_REQUEST_BODY_READ_DATA);
	assert(read == CK_HTTP_REQUEST_BODY_BUFFER_BYTES - 2u);
	assert(ck_http_request_body_read(
			&body, received, sizeof(received), &read)
			== CK_HTTP_REQUEST_BODY_READ_EOF);
	assert(ck_http_request_body_is_finished(&body));
	assert(ck_http_request_body_write(
			&body, source, 1, &written)
			== CK_HTTP_REQUEST_BODY_WRITE_CLOSED);

	ck_http_request_body_init(&body);
	ck_http_request_body_mark_error(&body);
	assert(ck_http_request_body_read(
			&body, received, sizeof(received), &read)
			== CK_HTTP_REQUEST_BODY_READ_ERROR);
	assert(ck_http_request_body_write(
			&body, source, 1, &written)
			== CK_HTTP_REQUEST_BODY_WRITE_CLOSED);

	return 0;
}
