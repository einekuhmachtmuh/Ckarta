#include "../jni/ck_jni_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>

static ck_runtime_t runtime;

static int check_result(const char *name, int result)
{
	if (result == 0)
	{
		return 0;
	}

	fprintf(stderr, "CKARTA_%s_ERROR=%d\n", name, result);
	return 1;
}

int main(int argc, char **argv)
{
	const char *class_path = getenv("CKARTA_CLASS_PATH");
	const unsigned char body[] = "ckarta-jni-smoke";
	ck_request_descriptor_t descriptor;
	ck_request_t request;
	ck_completion_t completion;
	int result;
	int completion_result;

	(void)argc;
	(void)argv;

	if (class_path == NULL)
	{
		class_path = "build/classes";
	}

	memset(&descriptor, 0, sizeof(descriptor));
	descriptor.abi_version = CK_JNI_ABI_VERSION;
	descriptor.struct_size = sizeof(descriptor);
	descriptor.feature_flags = CK_REQUEST_FEATURE_DIRECT_BUFFER;
	descriptor.ownership_flags = CK_REQUEST_OWNS_NATIVE_STORAGE |
			CK_REQUEST_JAVA_BORROWS_BUFFER;
	descriptor.owner_token = 1;
	descriptor.lifetime_token = 1;
	descriptor.request_id = UINT64_C(0xC4A7A);
	descriptor.body = body;
	descriptor.body_length = sizeof(body) - 1;

	ck_completion_init(&completion);

	result = ck_request_init(&request, &descriptor);
	if (check_result("REQUEST_INIT", result) != 0)
	{
		return EXIT_FAILURE;
	}

	result = ck_runtime_init(&runtime, class_path);
	if (check_result("INIT", result) != 0)
	{
		return EXIT_FAILURE;
	}

	result = ck_runtime_dispatch_async_smoke(&runtime, &request, &completion);
	if (check_result("DISPATCH_SUBMIT", result) != 0)
	{
		ck_runtime_shutdown(&runtime);
		ck_runtime_destroy(&runtime);
		return EXIT_FAILURE;
	}

	do
	{
		completion_result = ck_runtime_poll_completion(&request, &completion);
		if (completion_result == 0)
		{
			sched_yield();
		}
	} while (completion_result == 0);

	result = completion_result == 1 ? 0 : -1;

	if (check_result("DISPATCH", result) != 0)
	{
		if (ck_request_state(&request) == CK_REQUEST_RUNNING)
		{
			(void)ck_request_cancel(&request);
		}
		ck_runtime_shutdown(&runtime);
		ck_runtime_destroy(&runtime);
		return EXIT_FAILURE;
	}

	result = ck_runtime_shutdown(&runtime);
	if (check_result("SHUTDOWN", result) != 0)
	{
		ck_runtime_destroy(&runtime);
		return EXIT_FAILURE;
	}

	ck_runtime_destroy(&runtime);
	if (ck_request_state(&request) != CK_REQUEST_COMPLETED)
	{
		fprintf(stderr, "CKARTA_REQUEST_STATE_ERROR=%d\n",
				(int)ck_request_state(&request));
		return EXIT_FAILURE;
	}
	printf("CKARTA_SMOKE_OK\n");
	return EXIT_SUCCESS;
}
