#include "../c/jni/ck_error.h"

#include <assert.h>
#include <stdint.h>

int main(void)
{
	ck_error_t error;

	ck_error_init(&error);
	assert(error.abi_version == CK_ERROR_ABI_VERSION);
	assert(error.category == CK_ERROR_CATEGORY_NONE);
	assert(error.code == CK_ERROR_CODE_NONE);
	assert(error.http_status == 0);
	assert(error.flags == 0);
	assert(error.phase == 0);
	assert(error.request_id == 0);

	assert(ck_error_set(&error, CK_ERROR_CATEGORY_RESOURCE,
			CK_ERROR_CODE_RESOURCE_EXHAUSTED, 503, 0, 2, 19) == 0);
	assert(error.category == CK_ERROR_CATEGORY_RESOURCE);
	assert(error.code == CK_ERROR_CODE_RESOURCE_EXHAUSTED);
	assert(error.http_status == 503);
	assert(error.phase == 2);
	assert(error.request_id == 19);

	assert(ck_error_set(&error, CK_ERROR_CATEGORY_JNI,
			CK_ERROR_CODE_JNI_FAILURE, 500, 0, 3, 19) == 0);
	assert(ck_error_set(&error, CK_ERROR_CATEGORY_JNI,
			CK_ERROR_CODE_JNI_FAILURE, 600, 0, 3, 19) == -1);

	return 0;
}
