#include "ck_error.h"

#include <stddef.h>

static int ck_error_category_valid(ck_error_category_t category)
{
	return category >= CK_ERROR_CATEGORY_NONE
			&& category <= CK_ERROR_CATEGORY_FATAL;
}

static int ck_error_code_valid(ck_error_code_t code)
{
	return code >= CK_ERROR_CODE_NONE
			&& code <= CK_ERROR_CODE_FATAL_RUNTIME;
}

void ck_error_init(ck_error_t *error)
{
	if (error == NULL)
	{
		return;
	}

	error->abi_version = CK_ERROR_ABI_VERSION;
	error->category = CK_ERROR_CATEGORY_NONE;
	error->code = CK_ERROR_CODE_NONE;
	error->http_status = 0;
	error->flags = 0;
	error->phase = 0;
	error->request_id = 0;
}

int ck_error_set(ck_error_t *error, ck_error_category_t category,
		ck_error_code_t code, int32_t http_status, uint32_t flags,
		uint32_t phase, uint64_t request_id)
{
	if (error == NULL || !ck_error_category_valid(category)
			|| !ck_error_code_valid(code))
	{
		return -1;
	}

	if (http_status != 0 && (http_status < 100 || http_status > 599))
	{
		return -1;
	}

	error->abi_version = CK_ERROR_ABI_VERSION;
	error->category = (uint32_t)category;
	error->code = (uint32_t)code;
	error->http_status = http_status;
	error->flags = flags;
	error->phase = phase;
	error->request_id = request_id;
	return 0;
}
