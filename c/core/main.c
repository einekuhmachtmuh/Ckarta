#include "../jni/ck_jni_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
	int result;

	(void)argc;
	(void)argv;

	if (class_path == NULL)
	{
		class_path = "build/classes";
	}

	result = ck_runtime_init(&runtime, class_path);
	if (check_result("INIT", result) != 0)
	{
		return EXIT_FAILURE;
	}

	result = ck_runtime_dispatch_smoke(&runtime, (uintptr_t)0xC4A7A, body,
			sizeof(body) - 1);
	if (check_result("DISPATCH", result) != 0)
	{
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
	printf("CKARTA_SMOKE_OK\n");
	return EXIT_SUCCESS;
}
