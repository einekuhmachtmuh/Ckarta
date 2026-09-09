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

	{
		ck_request_t empty_body_request;
		ck_request_descriptor_t empty = descriptor;
		empty.body = NULL;
		empty.body_length = 0;
		assert(ck_request_init(&empty_body_request, &empty) == 0);
	}

	{
		static const char raw_request[] =
				"GET /handoff HTTP/1.1\r\nHost: x\r\n\r\n";
		ck_http_parser_t parser;
		ck_http_request_t parsed;
		ck_request_t http_request;
		size_t consumed = 0;

		ck_http_parser_init(&parser);
		assert(ck_http_parser_feed(&parser, raw_request,
				sizeof(raw_request) - 1, &consumed, &parsed)
				== CK_HTTP_PARSE_COMPLETE);
		assert(consumed == sizeof(raw_request) - 1);
		assert(ck_request_init_http(&http_request, &parsed,
				NULL, 0, 91, 92, 93) == 0);
		assert(http_request.descriptor.metadata != NULL);
		assert(http_request.descriptor.metadata_length >= 13);
		assert(http_request.descriptor.body == NULL);
		assert(http_request.descriptor.body_length == 0);
		assert(http_request.descriptor.feature_flags
				& CK_REQUEST_FEATURE_METADATA_BUFFER);

		{
			ck_request_descriptor_t invalid = http_request.descriptor;
			invalid.feature_flags &= ~CK_REQUEST_FEATURE_METADATA_BUFFER;
			assert(ck_request_init(&http_request, &invalid) == -1);
		}
	}
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
	assert(ck_request_cancel(&request) == 1);
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
	assert(ck_request_cancel(&request) == 1);

	return 0;
}
