#include "../config/ck_config.h"
#include "../jni/ck_jni_runtime.h"
#include "../http/ck_http_parser.h"
#include "../event/ck_event_loop.h"

#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CKARTA_DEFAULT_CONFIG_PATH "conf/ckarta.conf"

static int check_result(const char *name, int result)
{
	if (result == 0)
	{
		return 0;
	}

	fprintf(stderr, "CKARTA_%s_ERROR=%d\n", name, result);
	return 1;
}

static void print_usage(const char *program)
{
	printf("Usage: %s [-c config] [-t|-T] [-h]\n", program);
	printf("  -c config  use an alternative configuration file\n");
	printf("  -t         test configuration and exit\n");
	printf("  -T         test configuration, dump it, and exit\n");
	printf("  -h         print this help\n");
}

static int parse_arguments(int argc, char **argv,
		const char **config_path, int *test_only, int *dump_config)
{
	int i;

	for (i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-c") == 0)
		{
			if (i + 1 >= argc)
			{
				return -1;
			}

			*config_path = argv[++i];
			continue;
		}

		if (strcmp(argv[i], "-t") == 0)
		{
			*test_only = 1;
			continue;
		}

		if (strcmp(argv[i], "-T") == 0)
		{
			*test_only = 1;
			*dump_config = 1;
			continue;
		}

		if (strcmp(argv[i], "-h") == 0 ||
				strcmp(argv[i], "--help") == 0)
		{
			return 1;
		}

		return -1;
	}

	return 0;
}

static int apply_class_path(void *data, size_t argc,
		const char *const *argv, size_t line, char *error, size_t error_size)
{
	if (argc != 1)
	{
		(void)snprintf(error, error_size, "line %zu: class_path requires one value",
				line);
		return -1;
	}

	if (ck_config_set_class_path(data, argv[0], error, error_size) != 0)
	{
		char message[512];

		(void)snprintf(message, sizeof(message), "%s", error);
		(void)snprintf(error, error_size, "line %zu: %s", line, message);
		return -1;
	}

	return 0;
}

int main(int argc, char **argv)
{
	static const char request_one[] =
			"GET /handoff-one HTTP/1.1\r\nHost: x\r\n\r\n";
	static const char request_two[] =
			"GET /handoff-two HTTP/1.1\r\nHost: x\r\n\r\n";
	const char *config_path = CKARTA_DEFAULT_CONFIG_PATH;
	ck_config_t config;
	ck_runtime_t runtime;
	ck_request_t requests[2];
	char config_error[512];
	int test_only = 0;
	int dump_config = 0;
	int result;
	int completion_result;
	int completed_count = 0;

	result = parse_arguments(argc, argv, &config_path,
			&test_only, &dump_config);
	if (result > 0)
	{
		print_usage(argv[0]);
		return EXIT_SUCCESS;
	}
	if (result < 0)
	{
		print_usage(argv[0]);
		return EXIT_FAILURE;
	}

	memset(&config, 0, sizeof(config));
	memset(&runtime, 0, sizeof(runtime));
	memset(config_error, 0, sizeof(config_error));
	result = ck_config_init(&config);
	if (check_result("CONFIG_INIT", result) != 0)
	{
		return EXIT_FAILURE;
	}

	{
		static const ck_config_directive_t directives[] = {
			{ "class_path", apply_class_path }
		};

		result = ck_config_load_file(&config, config_path,
				directives,
				sizeof(directives) / sizeof(directives[0]),
				config_error, sizeof(config_error));
	}
	if (result != 0)
	{
		fprintf(stderr, "CKARTA_CONFIG_ERROR=%s\n", config_error);
		ck_config_destroy(&config);
		return EXIT_FAILURE;
	}

	if (dump_config)
	{
		result = ck_config_dump(&config);
		ck_config_destroy(&config);
		return check_result("CONFIG_DUMP", result) == 0 ?
				EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (test_only)
	{
		printf("CKARTA_CONFIG_OK\n");
		ck_config_destroy(&config);
		return EXIT_SUCCESS;
	}

	{
		ck_http_parser_t parser;
		ck_http_request_t parsed;
		size_t consumed = 0;

		ck_http_parser_init(&parser);
		result = ck_http_parser_feed(&parser, request_one,
				sizeof(request_one) - 1, &consumed, &parsed);
		if (result != CK_HTTP_PARSE_COMPLETE
				|| consumed != sizeof(request_one) - 1)
		{
			check_result("REQUEST_PARSE_1", -1);
			ck_config_destroy(&config);
			return EXIT_FAILURE;
		}
		result = ck_request_init_http(&requests[0], &parsed,
				NULL, 0, UINT64_C(0xC4A7A), 1, 11);
		if (check_result("REQUEST_INIT_1", result) != 0)
		{
			ck_config_destroy(&config);
			return EXIT_FAILURE;
		}

		ck_http_parser_init(&parser);
		result = ck_http_parser_feed(&parser, request_two,
				sizeof(request_two) - 1, &consumed, &parsed);
		if (result != CK_HTTP_PARSE_COMPLETE
				|| consumed != sizeof(request_two) - 1)
		{
			check_result("REQUEST_PARSE_2", -1);
			ck_config_destroy(&config);
			return EXIT_FAILURE;
		}
		result = ck_request_init_http(&requests[1], &parsed,
				NULL, 0, UINT64_C(0xC4A7B), 2, 22);
		if (check_result("REQUEST_INIT_2", result) != 0)
		{
			ck_config_destroy(&config);
			return EXIT_FAILURE;
		}
	}

	result = ck_runtime_init(&runtime, config.class_path);
	if (check_result("INIT", result) != 0)
	{
		ck_config_destroy(&config);
		return EXIT_FAILURE;
	}

	result = ck_runtime_dispatch_async_smoke(&runtime, requests, 2);
	if (check_result("DISPATCH_SUBMIT", result) != 0)
	{
		ck_runtime_shutdown(&runtime);
		ck_runtime_destroy(&runtime);
		ck_config_destroy(&config);
		return EXIT_FAILURE;
	}

	{
		ck_event_loop_t event_loop;
		ck_event_notification_t notification;
		int wait_result;
		int event_loop_ready = 0;

		memset(&event_loop, 0, sizeof(event_loop));
		memset(&notification, 0, sizeof(notification));
		result = ck_event_loop_init(&event_loop);
		if (result != 0)
		{
			ck_runtime_shutdown(&runtime);
			ck_runtime_destroy(&runtime);
			ck_config_destroy(&config);
			return EXIT_FAILURE;
		}
		event_loop_ready = 1;

		result = ck_event_loop_add(&event_loop,
				ck_runtime_completion_fd(&runtime), 1, CK_EVENT_READ);
		if (result != 0)
		{
			(void)ck_event_loop_destroy(&event_loop);
			ck_runtime_shutdown(&runtime);
			ck_runtime_destroy(&runtime);
			ck_config_destroy(&config);
			return EXIT_FAILURE;
		}

		while (completed_count < 2)
		{
			wait_result = ck_event_loop_wait(&event_loop,
					&notification, 1, -1);
			if (wait_result == -EINTR)
			{
				continue;
			}
			if (wait_result < 0)
			{
				break;
			}
			if (wait_result == 0)
			{
				continue;
			}

			if (ck_runtime_drain_completion_notification(&runtime) != 0)
			{
				break;
			}

			while ((completion_result =
					ck_runtime_poll_completion(&runtime, requests, 2)) > 0)
			{
				if (completion_result == 1)
				{
					completed_count++;
				}
			}
			if (completion_result < 0)
			{
				break;
			}
		}

		if (event_loop_ready)
		{
			(void)ck_event_loop_destroy(&event_loop);
		}
	}

	result = completed_count == 2 ? 0 : -1;
	if (check_result("DISPATCH", result) != 0)
	{
		ck_runtime_shutdown(&runtime);
		ck_runtime_destroy(&runtime);
		ck_config_destroy(&config);
		return EXIT_FAILURE;
	}

	result = ck_runtime_shutdown(&runtime);
	if (check_result("SHUTDOWN", result) != 0)
	{
		ck_runtime_destroy(&runtime);
		ck_config_destroy(&config);
		return EXIT_FAILURE;
	}

	result = ck_runtime_shutdown(&runtime);
	if (check_result("SHUTDOWN_AGAIN", result) != 0)
	{
		ck_runtime_destroy(&runtime);
		ck_config_destroy(&config);
		return EXIT_FAILURE;
	}

	ck_runtime_destroy(&runtime);
	ck_config_destroy(&config);
	printf("CKARTA_SMOKE_OK\n");
	return EXIT_SUCCESS;
}
