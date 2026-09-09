#include "../c/jni/ck_request.h"

#include <assert.h>
#include <string.h>

int main(void)
{
	unsigned char body[] = "lifecycle";
	ck_request_descriptor_t descriptor;
	ck_request_t request;

	memset(&descriptor, 0, sizeof(descriptor));
	descriptor.abi_version = CK_JNI_ABI_VERSION;
	descriptor.struct_size = sizeof(descriptor);
	descriptor.feature_flags = CK_REQUEST_FEATURE_DIRECT_BUFFER;
	descriptor.ownership_flags = CK_REQUEST_OWNS_NATIVE_STORAGE |
			CK_REQUEST_JAVA_BORROWS_BUFFER;
	descriptor.owner_token = 7;
	descriptor.lifetime_token = 11;
	descriptor.request_id = 19;
	descriptor.body = body;
	descriptor.body_length = sizeof(body) - 1;

	assert(ck_request_init(&request, &descriptor) == 0);
	assert(ck_request_state(&request) == CK_REQUEST_PENDING);
	assert(ck_request_state(NULL) == CK_REQUEST_STATE_INVALID);

	{
		ck_request_descriptor_t invalid = descriptor;
		invalid.body = NULL;
		assert(ck_request_init(&request, &invalid) == -1);

		invalid = descriptor;
		invalid.body_length = (uint64_t)INT32_MAX + 1;
		assert(ck_request_init(&request, &invalid) == -1);
	}
	assert(ck_request_begin(&request) == 0);
	assert(ck_request_state(&request) == CK_REQUEST_RUNNING);

	assert(ck_request_cancel(&request) == 0);
	assert(ck_request_cancel(&request) == 0);
	assert(ck_request_state(&request) == CK_REQUEST_CANCELLING);
	assert(ck_request_finish(&request) == 1);
	assert(ck_request_state(&request) == CK_REQUEST_CANCELLING);

	assert(ck_request_init(&request, &descriptor) == 0);
	assert(ck_request_finish(&request) == -1);
	assert(ck_request_begin(&request) == 0);
	assert(ck_request_finish(&request) == 0);

	assert(ck_request_init(&request, &descriptor) == 0);
	assert(ck_request_begin(&request) == 0);
	{
		ck_error_t error;

		ck_error_init(&error);
		assert(ck_error_set(&error, CK_ERROR_CATEGORY_APPLICATION,
				CK_ERROR_CODE_APPLICATION_EXCEPTION, 500,
				CK_ERROR_FLAG_CLIENT_VISIBLE, 2, descriptor.request_id) == 0);
		assert(ck_request_fail(&request, &error) == 0);
		assert(ck_request_state(&request) == CK_REQUEST_FAILED);
		assert(ck_request_error(&request) != NULL);
		assert(ck_request_error(&request)->code
				== CK_ERROR_CODE_APPLICATION_EXCEPTION);
		assert(ck_request_finish(&request) == 1);
	}
	assert(ck_request_state(&request) == CK_REQUEST_FAILED);
	assert(ck_request_cancel(&request) == 0);

	return 0;
}
