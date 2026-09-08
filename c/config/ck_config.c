#include "ck_config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CK_CONFIG_LINE_SIZE 4096
#define CK_CONFIG_MAX_ARGS 8
#define CK_CONFIG_DEFAULT_CLASS_PATH "build/classes"

static int ck_config_set_error(char *error, size_t error_size,
		size_t line, const char *message)
{
	if (error != NULL && error_size > 0)
	{
		(void)snprintf(error, error_size, "line %zu: %s", line, message);
	}

	return -1;
}

static char *ck_config_copy_string(const char *value, size_t length)
{
	char *copy;

	copy = malloc(length + 1);
	if (copy == NULL)
	{
		return NULL;
	}

	memcpy(copy, value, length);
	copy[length] = '\0';
	return copy;
}

int ck_config_set_class_path(ck_config_t *config, const char *value,
		char *error, size_t error_size)
{
	char *path;

	if (config == NULL || value == NULL || value[0] == '\0')
	{
		if (error != NULL && error_size > 0)
		{
			(void)snprintf(error, error_size,
					"class_path requires a non-empty value");
		}
		return -1;
	}

	if (config->class_path_configured)
	{
		if (error != NULL && error_size > 0)
		{
			(void)snprintf(error, error_size,
					"duplicate class_path directive");
		}
		return -1;
	}

	path = ck_config_copy_string(value, strlen(value));
	if (path == NULL)
	{
		if (error != NULL && error_size > 0)
		{
			(void)snprintf(error, error_size, "out of memory");
		}
		return -1;
	}

	free(config->class_path);
	config->class_path = path;
	config->class_path_configured = 1;
	return 0;
}

static int ck_config_class_path(void *data, size_t argc,
		const char *const *argv, size_t line, char *error, size_t error_size)
{
	int result;

	if (argc != 1)
	{
		return ck_config_set_error(error, error_size, line,
				"class_path requires exactly one value");
	}

	result = ck_config_set_class_path(data, argv[0], error, error_size);
	if (result != 0 && error != NULL && error_size > 0 && line != 0)
	{
		char message[512];

		(void)snprintf(message, sizeof(message), "%s", error);
		(void)snprintf(error, error_size, "line %zu: %s", line, message);
	}

	return result;
}

static int ck_config_parse_token(char **cursor, char *token,
		size_t token_size, size_t line, char *error, size_t error_size)
{
	char *p = *cursor;
	size_t length = 0;
	int quoted = 0;

	if (*p == '"')
	{
		quoted = 1;
		p++;
	}

	while (*p != '\0')
	{
		char ch = *p;

		if (quoted)
		{
			if (ch == '"')
			{
				p++;
				quoted = 0;
				break;
			}

			if (ch == '\\')
			{
				p++;
				if (*p == '\0')
				{
					return ck_config_set_error(error, error_size, line,
							"unterminated escape");
				}

				switch (*p)
				{
				case '"':
				case '\\':
					ch = *p;
					break;
				case 'n':
					ch = '\n';
					break;
				case 't':
					ch = '\t';
					break;
				default:
					return ck_config_set_error(error, error_size, line,
							"unsupported escape");
				}
			}

			if (length + 1 >= token_size)
			{
				return ck_config_set_error(error, error_size, line,
						"token is too long");
			}

			token[length++] = ch;
			p++;
			continue;
		}

		if (isspace((unsigned char)ch) || ch == ';' || ch == '#')
		{
			break;
		}

		if (length + 1 >= token_size)
		{
			return ck_config_set_error(error, error_size, line,
					"token is too long");
		}

		token[length++] = ch;
		p++;
	}

	if (quoted)
	{
		return ck_config_set_error(error, error_size, line,
				"unterminated quoted string");
	}

	token[length] = '\0';
	*cursor = p;
	return length == 0 ? -1 : 0;
}

static const ck_config_directive_t *
ck_config_find_directive(const ck_config_directive_t *directives,
		size_t directive_count, const char *name)
{
	size_t i;

	for (i = 0; i < directive_count; i++)
	{
		if (strcmp(directives[i].name, name) == 0)
		{
			return &directives[i];
		}
	}

	return NULL;
}

int ck_config_init(ck_config_t *config)
{
	if (config == NULL)
	{
		return -1;
	}

	config->class_path = ck_config_copy_string(
			CK_CONFIG_DEFAULT_CLASS_PATH,
			sizeof(CK_CONFIG_DEFAULT_CLASS_PATH) - 1);
	if (config->class_path == NULL)
	{
		return -1;
	}

	config->class_path_configured = 0;
	return 0;
}

int ck_config_load_file(ck_config_t *config, const char *path,
		const ck_config_directive_t *directives, size_t directive_count,
		char *error, size_t error_size)
{
	FILE *file;
	char line_buffer[CK_CONFIG_LINE_SIZE];
	size_t line = 0;

	if (config == NULL || path == NULL || directives == NULL ||
			directive_count == 0)
	{
		return -1;
	}

	file = fopen(path, "r");
	if (file == NULL)
	{
		if (error != NULL && error_size > 0)
		{
			(void)snprintf(error, error_size,
					"cannot open configuration: %s", strerror(errno));
		}
		return -1;
	}

	while (fgets(line_buffer, sizeof(line_buffer), file) != NULL)
	{
		char *cursor = line_buffer;
		char *end;
		char directive_name[CK_CONFIG_LINE_SIZE];
		char argument_storage[CK_CONFIG_MAX_ARGS][CK_CONFIG_LINE_SIZE];
		const char *arguments[CK_CONFIG_MAX_ARGS];
		size_t argc = 0;
		const ck_config_directive_t *directive;
		size_t i;

		line++;
		end = strchr(line_buffer, '\n');
		if (end == NULL && !feof(file))
		{
			(void)fclose(file);
			return ck_config_set_error(error, error_size, line,
					"configuration line is too long");
		}

		while (isspace((unsigned char)*cursor))
		{
			cursor++;
		}

		if (*cursor == '\0' || *cursor == '#')
		{
			continue;
		}

		if (*cursor == '{' || *cursor == '}')
		{
			(void)fclose(file);
			return ck_config_set_error(error, error_size, line,
					"blocks are not supported in this configuration version");
		}

		if (ck_config_parse_token(&cursor, directive_name,
				sizeof(directive_name), line, error, error_size) != 0)
		{
			(void)fclose(file);
			return -1;
		}

		while (1)
		{
			while (isspace((unsigned char)*cursor))
			{
				cursor++;
			}

			if (*cursor == '#')
			{
				(void)fclose(file);
				return ck_config_set_error(error, error_size, line,
						"missing semicolon");
			}

			if (*cursor == ';')
			{
				cursor++;
				break;
			}

			if (*cursor == '\0')
			{
				(void)fclose(file);
				return ck_config_set_error(error, error_size, line,
						"missing semicolon");
			}

			if (argc == CK_CONFIG_MAX_ARGS)
			{
				(void)fclose(file);
				return ck_config_set_error(error, error_size, line,
						"too many directive arguments");
			}

			if (ck_config_parse_token(&cursor,
					argument_storage[argc],
					sizeof(argument_storage[argc]),
					line, error, error_size) != 0)
			{
				(void)fclose(file);
				return -1;
			}

			arguments[argc] = argument_storage[argc];
			argc++;
		}

		while (isspace((unsigned char)*cursor))
		{
			cursor++;
		}

		if (*cursor != '\0' && *cursor != '#')
		{
			(void)fclose(file);
			return ck_config_set_error(error, error_size, line,
					"unexpected characters after semicolon");
		}

		directive = ck_config_find_directive(directives,
				directive_count, directive_name);
		if (directive == NULL || directive->handler == NULL)
		{
			(void)fclose(file);
			return ck_config_set_error(error, error_size, line,
					"unknown or invalid directive");
		}

		for (i = 0; i < argc; i++)
		{
			arguments[i] = argument_storage[i];
		}

		if (directive->handler(config, argc, arguments,
				line, error, error_size) != 0)
		{
			(void)fclose(file);
			return -1;
		}
	}

	if (ferror(file))
	{
		(void)fclose(file);
		return ck_config_set_error(error, error_size, line,
				"error while reading configuration");
	}

	(void)fclose(file);
	return 0;
}

int ck_config_dump(const ck_config_t *config)
{
	if (config == NULL || config->class_path == NULL)
	{
		return -1;
	}

	printf("class_path \"%s\";\n", config->class_path);
	return 0;
}

void ck_config_destroy(ck_config_t *config)
{
	if (config == NULL)
	{
		return;
	}

	free(config->class_path);
	config->class_path = NULL;
	config->class_path_configured = 0;
}
