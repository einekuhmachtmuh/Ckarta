#ifndef CKARTA_ERROR_H
#define CKARTA_ERROR_H

#include <stdint.h>

#define CK_ERROR_ABI_VERSION 1u
#define CK_ERROR_FLAG_RETRYABLE (UINT32_C(1) << 0)
#define CK_ERROR_FLAG_CLIENT_VISIBLE (UINT32_C(1) << 1)

typedef enum ck_error_category
{
	CK_ERROR_CATEGORY_NONE = 0,
	CK_ERROR_CATEGORY_INPUT = 1,
	CK_ERROR_CATEGORY_POLICY = 2,
	CK_ERROR_CATEGORY_RESOURCE = 3,
	CK_ERROR_CATEGORY_TIMEOUT = 4,
	CK_ERROR_CATEGORY_CANCELLATION = 5,
	CK_ERROR_CATEGORY_UPSTREAM = 6,
	CK_ERROR_CATEGORY_APPLICATION = 7,
	CK_ERROR_CATEGORY_JNI = 8,
	CK_ERROR_CATEGORY_INTERNAL = 9,
	CK_ERROR_CATEGORY_FATAL = 10
} ck_error_category_t;

typedef enum ck_error_code
{
	CK_ERROR_CODE_NONE = 0,
	CK_ERROR_CODE_INVALID_INPUT = 1,
	CK_ERROR_CODE_POLICY_REJECTED = 2,
	CK_ERROR_CODE_RESOURCE_EXHAUSTED = 3,
	CK_ERROR_CODE_TIMEOUT = 4,
	CK_ERROR_CODE_CANCELLED = 5,
	CK_ERROR_CODE_UPSTREAM_FAILURE = 6,
	CK_ERROR_CODE_APPLICATION_EXCEPTION = 7,
	CK_ERROR_CODE_JNI_FAILURE = 8,
	CK_ERROR_CODE_INTERNAL_INVARIANT = 9,
	CK_ERROR_CODE_FATAL_RUNTIME = 10
} ck_error_code_t;

typedef struct ck_error
{
	uint32_t abi_version;
	uint32_t category;
	uint32_t code;
	int32_t http_status;
	uint32_t flags;
	uint32_t phase;
	uint64_t request_id;
} ck_error_t;

_Static_assert(sizeof(ck_error_t) == 32, "ck_error_t layout must remain 32 bytes");

void ck_error_init(ck_error_t *error);
int ck_error_set(ck_error_t *error, ck_error_category_t category,
		ck_error_code_t code, int32_t http_status, uint32_t flags,
		uint32_t phase, uint64_t request_id);

#endif