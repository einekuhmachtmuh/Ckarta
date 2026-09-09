#include "ck_http_response.h"

#include <limits.h>
#include <string.h>

static int ck_http_response_valid_status(uint16_t status)
{
	return status >= 200U && status <= 599U;
}

static void ck_http_response_fail(ck_http_response_t *response)
{
	response->state = CK_HTTP_RESPONSE_FAILED;
}

void ck_http_response_init(ck_http_response_t *response)
{
	if (response == NULL)
	{
		return;
	}

	memset(response, 0, sizeof(*response));
	response->state = CK_HTTP_RESPONSE_NEW;
	response->status = 500U;
}

ck_http_response_state_t ck_http_response_state(
	const ck_http_response_t *response)
{
	if (response == NULL)
	{
		return CK_HTTP_RESPONSE_FAILED;
	}
	return response->state;
}

int ck_http_response_set_status(ck_http_response_t *response,
	uint16_t status)
{
	if (response == NULL || !ck_http_response_valid_status(status)
			|| response->state != CK_HTTP_RESPONSE_NEW)
	{
		return -1;
	}

	response->status = status;
	return 0;
}

int ck_http_response_set_content_length(ck_http_response_t *response,
	uint64_t content_length)
{
	if (response == NULL || response->state != CK_HTTP_RESPONSE_NEW
			|| content_length > SIZE_MAX)
	{
		return -1;
	}

	response->content_length = content_length;
	response->content_length_set = 1;
	return 0;
}

int ck_http_response_set_connection_close(ck_http_response_t *response,
	int enabled)
{
	if (response == NULL || response->state != CK_HTTP_RESPONSE_NEW
			|| (enabled != 0 && enabled != 1))
	{
		return -1;
	}

	response->connection_close = enabled;
	return 0;
}

int ck_http_response_reset(ck_http_response_t *response)
{
	if (response == NULL || response->state != CK_HTTP_RESPONSE_NEW)
	{
		return -1;
	}

	response->status = 500U;
	response->content_length = 0;
	response->content_length_set = 0;
	response->connection_close = 0;
	response->body_length = 0;
	return 0;
}

int ck_http_response_write_body(ck_http_response_t *response,
	const unsigned char *data,
	size_t length)
{
	if (response == NULL || (data == NULL && length != 0)
			|| (response->state != CK_HTTP_RESPONSE_NEW
				&& response->state != CK_HTTP_RESPONSE_COMMITTED))
	{
		return -1;
	}

	if (response->state == CK_HTTP_RESPONSE_NEW
			&& ck_http_response_commit(response) != 0)
	{
		return -1;
	}

	if (length > sizeof(response->body) - response->body_length)
	{
		ck_http_response_fail(response);
		return -2;
	}

	if (length != 0)
	{
		memcpy(response->body + response->body_length, data, length);
		response->body_length += length;
	}
	return 0;
}

int ck_http_response_commit(ck_http_response_t *response)
{
	if (response == NULL || response->state != CK_HTTP_RESPONSE_NEW
			|| !ck_http_response_valid_status(response->status))
	{
		return -1;
	}

	response->state = CK_HTTP_RESPONSE_COMMITTED;
	return 0;
}

int ck_http_response_finish(ck_http_response_t *response)
{
	if (response == NULL || response->state != CK_HTTP_RESPONSE_COMMITTED)
	{
		return -1;
	}

	if (response->content_length_set
			&& response->body_length != response->content_length)
	{
		ck_http_response_fail(response);
		return -2;
	}

	response->state = CK_HTTP_RESPONSE_FINISHED;
	return 0;
}

const unsigned char *ck_http_response_body(
	const ck_http_response_t *response)
{
	return response == NULL ? NULL : response->body;
}

size_t ck_http_response_body_length(
	const ck_http_response_t *response)
{
	return response == NULL ? 0 : response->body_length;
}
