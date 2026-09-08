#include "../c/config/ck_config.h"

#include <assert.h>
#include <string.h>

static int apply_class_path(void *data, size_t argc,
		const char *const *argv, size_t line, char *error, size_t error_size)
{
	(void)line;

	if (argc != 1)
	{
		if (error != NULL && error_size > 0)
		{
			(void)snprintf(error, error_size, "class_path requires one value");
		}
		return -1;
	}

	return ck_config_set_class_path(data, argv[0], error, error_size);
}

int main(int argc, char **argv)
{
	static const ck_config_directive_t directives[] = {
		{ "class_path", apply_class_path }
	};
	ck_config_t config;
	char error[256];

	assert(argc == 2);
	memset(&config, 0, sizeof(config));
	memset(error, 0, sizeof(error));
	assert(ck_config_init(&config) == 0);

	if (ck_config_load_file(&config, argv[1], directives,
			sizeof(directives) / sizeof(directives[0]),
			error, sizeof(error)) == 0)
	{
		assert(strcmp(config.class_path, "build/classes") == 0 ||
				strcmp(config.class_path, "configured/classes") == 0);
		ck_config_destroy(&config);
		return 0;
	}

	ck_config_destroy(&config);
	return 1;
}
