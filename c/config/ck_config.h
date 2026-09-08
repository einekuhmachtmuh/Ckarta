#ifndef CKARTA_CONFIG_H
#define CKARTA_CONFIG_H

#include <stddef.h>

typedef struct ck_config
{
	char *class_path;
	int class_path_configured;
} ck_config_t;

typedef int (*ck_config_directive_handler_pt)(
		void *data, size_t argc, const char *const *argv,
		size_t line, char *error, size_t error_size);

typedef struct ck_config_directive
{
	const char *name;
	ck_config_directive_handler_pt handler;
} ck_config_directive_t;

int ck_config_init(ck_config_t *config);
int ck_config_set_class_path(ck_config_t *config, const char *value,
		char *error, size_t error_size);
int ck_config_load_file(ck_config_t *config, const char *path,
		const ck_config_directive_t *directives, size_t directive_count,
		char *error, size_t error_size);
int ck_config_dump(const ck_config_t *config);
void ck_config_destroy(ck_config_t *config);

#endif
