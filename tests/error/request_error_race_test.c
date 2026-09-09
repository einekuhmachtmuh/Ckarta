#include "../../c/jni/ck_request.h"

#include <assert.h>
#include <pthread.h>
#include <string.h>

typedef struct
{
	ck_request_t *request;
	ck_error_code_t code;
	int result;
} fail_thread_args_t;

static void *fail_thread(void *arg)
{
	fail_thread_args_t *args = arg;
	ck_error_t error;

	ck_error_init(&error);
	assert(ck_error_set(&error, CK_ERROR_CATEGORY_APPLICATION, args->code,
			500, CK_ERROR_FLAG_CLIENT_VISIBLE, 3,
			args->request->descriptor.request_id) == 0);
	args->result = ck_request_fail(args->request, &error);
	return NULL;
}

int main(void)
{
	unsigned char body[] = "race";
	ck_request_descriptor_t descriptor;
	ck_request_t request;
	fail_thread_args_t left;
	fail_thread_args_t right;
	pthread_t left_thread;
	pthread_t right_thread;
	const ck_error_t *published;
	int winners;

	memset(&descriptor, 0, sizeof(descriptor));
	descriptor.abi_version = CK_JNI_ABI_VERSION;
	descriptor.struct_size = sizeof(descriptor);
	descriptor.feature_flags = CK_REQUEST_FEATURE_DIRECT_BUFFER;
	descriptor.ownership_flags = CK_REQUEST_OWNS_NATIVE_STORAGE |
			CK_REQUEST_JAVA_BORROWS_BUFFER;
	descriptor.owner_token = 1;
	descriptor.lifetime_token = 2;
	descriptor.request_id = 3;
	descriptor.body = body;
	descriptor.body_length = sizeof(body) - 1;

	assert(ck_request_init(&request, &descriptor) == 0);
	assert(ck_request_begin(&request) == 0);

	left.request = &request;
	left.code = CK_ERROR_CODE_APPLICATION_EXCEPTION;
	left.result = -99;
	right = left;
	right.code = CK_ERROR_CODE_JNI_FAILURE;

	assert(pthread_create(&left_thread, NULL, fail_thread, &left) == 0);
	assert(pthread_create(&right_thread, NULL, fail_thread, &right) == 0);
	assert(pthread_join(left_thread, NULL) == 0);
	assert(pthread_join(right_thread, NULL) == 0);

	winners = (left.result == 0) + (right.result == 0);
	assert(winners == 1);
	assert(ck_request_state(&request) == CK_REQUEST_FAILED);
	published = ck_request_error(&request);
	assert(published != NULL);
	assert(published->code == left.code || published->code == right.code);
	assert(ck_request_error(&request)->request_id == descriptor.request_id);

	return 0;
}
