#include "ck_http_parser.h"

#include <ctype.h>
#include <string.h>

static int is_tchar(unsigned char value)
{
	return (value >= '0' && value <= '9')
			|| (value >= 'A' && value <= 'Z')
			|| (value >= 'a' && value <= 'z')
			|| strchr("!#$%&'*+-.^_`|~", value) != NULL;
}

static int span_equal_ci(ck_http_span_t span, const char *literal)
{
	size_t length = strlen(literal);

	if (span.length != length)
	{
		return 0;
	}
	for (size_t i = 0; i < length; i++)
	{
		if (tolower((unsigned char)span.data[i]) !=
				tolower((unsigned char)literal[i]))
		{
			return 0;
		}
	}
	return 1;
}

static void trim_ows(ck_http_span_t *span)
{
	size_t start = 0;
	size_t end = span->length;

	while (start < end && (span->data[start] == ' '
			|| span->data[start] == '\t'))
	{
		start++;
	}
	while (end > start && (span->data[end - 1] == ' '
			|| span->data[end - 1] == '\t'))
	{
		end--;
	}
	span->data += start;
	span->length = end - start;
}

static int parse_uint64(ck_http_span_t span, uint64_t *value)
{
	uint64_t result = 0;

	if (span.length == 0)
	{
		return 0;
	}
	for (size_t i = 0; i < span.length; i++)
	{
		unsigned char digit = (unsigned char)span.data[i];
		if (digit < '0' || digit > '9')
		{
			return 0;
		}
		if (result > (UINT64_MAX - (uint64_t)(digit - '0')) / 10u)
		{
			return 0;
		}
		result = result * 10u + (uint64_t)(digit - '0');
	}
	*value = result;
	return 1;
}

static int find_crlf(const char *buffer, size_t length, size_t start,
	size_t *line_end)
{
	for (size_t i = start; i + 1 < length; i++)
	{
		if (buffer[i] == '\r' && buffer[i + 1] == '\n')
		{
			*line_end = i;
			return 1;
		}
	}
	return 0;
}

static int parse_request_line(const char *buffer, size_t line_end,
	ck_http_request_t *request)
{
	size_t method_end = 0;
	size_t target_start;
	size_t target_end;
	size_t version_start;

	while (method_end < line_end && buffer[method_end] != ' '
			&& buffer[method_end] != '\t')
	{
		method_end++;
	}
	if (method_end == 0 || method_end > CK_HTTP_MAX_METHOD_BYTES
			|| method_end == line_end)
	{
		return 0;
	}
	for (size_t i = 0; i < method_end; i++)
	{
		if (!is_tchar((unsigned char)buffer[i]))
		{
			return 0;
		}
	}

	target_start = method_end;
	while (target_start < line_end && (buffer[target_start] == ' '
			|| buffer[target_start] == '\t'))
	{
		target_start++;
	}
	target_end = target_start;
	while (target_end < line_end && buffer[target_end] != ' '
			&& buffer[target_end] != '\t')
	{
		target_end++;
	}
	if (target_end == target_start
			|| target_end - target_start > CK_HTTP_MAX_TARGET_BYTES)
	{
		return 0;
	}
	for (size_t i = target_start; i < target_end; i++)
	{
		unsigned char value = (unsigned char)buffer[i];
		if (value <= 0x20 || value == 0x7f)
		{
			return 0;
		}
	}

	version_start = target_end;
	while (version_start < line_end && (buffer[version_start] == ' '
			|| buffer[version_start] == '\t'))
	{
		version_start++;
	}
	if (version_start >= line_end
			|| line_end - version_start != sizeof("HTTP/1.1") - 1
			|| memcmp(buffer + version_start, "HTTP/1.1",
				sizeof("HTTP/1.1") - 1) != 0)
	{
		return 0;
	}

	request->method.data = buffer;
	request->method.length = method_end;
	request->target.data = buffer + target_start;
	request->target.length = target_end - target_start;
	request->version.data = buffer + version_start;
	request->version.length = line_end - version_start;
	return 1;
}

static int parse_content_length_headers(const ck_http_request_t *request,
	uint64_t *value, int *present)
{
	uint64_t canonical = 0;
	int found = 0;

	for (size_t i = 0; i < request->header_count; i++)
	{
		ck_http_span_t source;
		size_t start = 0;

		if (!span_equal_ci(request->headers[i].name, "Content-Length"))
		{
			continue;
		}
		source = request->headers[i].value;
		while (start < source.length)
		{
			size_t end = start;
			ck_http_span_t item;
			uint64_t parsed;

			while (start < source.length && (source.data[start] == ' '
					|| source.data[start] == '\t'))
			{
				start++;
			}
			end = start;
			while (end < source.length && source.data[end] != ',')
			{
				end++;
			}
			item.data = source.data + start;
			item.length = end - start;
			trim_ows(&item);
			if (!parse_uint64(item, &parsed))
			{
				return 0;
			}
			if (!found)
			{
				canonical = parsed;
				found = 1;
			}
			else if (canonical != parsed)
			{
				return 0;
			}
			start = end < source.length ? end + 1 : end;
		}
	}

	*value = canonical;
	*present = found;
	return 1;
}

static int parse_connection_headers(const ck_http_request_t *request,
	int *close_required)
{
	int close = 0;

	for (size_t i = 0; i < request->header_count; i++)
	{
		ck_http_span_t source;
		size_t start = 0;

		if (!span_equal_ci(request->headers[i].name, "Connection"))
		{
			continue;
		}
		source = request->headers[i].value;
		while (start < source.length)
		{
			size_t end = start;
			ck_http_span_t token;

			while (start < source.length && (source.data[start] == ' '
					|| source.data[start] == '\t'))
			{
				start++;
			}
			if (start == source.length || source.data[start] == ',')
			{
				return 0;
			}
			end = start;
			while (end < source.length && source.data[end] != ',')
			{
				end++;
			}
			token.data = source.data + start;
			token.length = end - start;
			trim_ows(&token);
			if (span_equal_ci(token, "close"))
			{
				close = 1;
			}
			start = end < source.length ? end + 1 : end;
		}
	}

	*close_required = close;
	return 1;
}

static int parse_transfer_encoding_headers(const ck_http_request_t *request,
	int *present, int *is_chunked)
{
	int found = 0;
	int chunked = 0;

	for (size_t i = 0; i < request->header_count; i++)
	{
		ck_http_span_t source;
		size_t start = 0;

		if (!span_equal_ci(request->headers[i].name, "Transfer-Encoding"))
		{
			continue;
		}
		found = 1;
		source = request->headers[i].value;
		while (start < source.length)
		{
			size_t end = start;
			ck_http_span_t token;

			while (start < source.length && (source.data[start] == ' '
					|| source.data[start] == '\t'))
			{
				start++;
			}
			if (start == source.length || source.data[start] == ',')
			{
				return 0;
			}
			end = start;
			while (end < source.length && source.data[end] != ',')
			{
				end++;
			}
			token.data = source.data + start;
			token.length = end - start;
			trim_ows(&token);
			if (!span_equal_ci(token, "chunked"))
			{
				return 0;
			}
			chunked = 1;
			start = end < source.length ? end + 1 : end;
		}
	}

	*present = found;
	*is_chunked = chunked;
	return 1;
}

static ck_http_parse_result_t parse_header_block(
	ck_http_parser_t *parser, size_t header_end, ck_http_request_t *request)
{
	size_t line_start = 0;
	size_t line_end;
	uint64_t content_length = 0;
	int has_content_length = 0;
	int has_transfer_encoding = 0;
	int transfer_is_chunked = 0;
	int connection_close = 0;

	memset(request, 0, sizeof(*request));
	if (!find_crlf(parser->buffer, header_end, 0, &line_end)
			|| !parse_request_line(parser->buffer, line_end, request))
	{
		return CK_HTTP_PARSE_BAD_REQUEST;
	}

	line_start = line_end + 2;
	while (line_start < header_end - 2)
	{
		ck_http_span_t name;
		ck_http_span_t value;
		size_t colon = line_start;

		if (!find_crlf(parser->buffer, header_end, line_start, &line_end))
		{
			return CK_HTTP_PARSE_BAD_REQUEST;
		}
		if (line_end == line_start)
		{
			break;
		}
		if (request->header_count >= CK_HTTP_MAX_HEADERS)
		{
			return CK_HTTP_PARSE_HEADER_TOO_LARGE;
		}
		while (colon < line_end && parser->buffer[colon] != ':')
		{
			colon++;
		}
		if (colon == line_start || colon >= line_end
				|| colon - line_start > CK_HTTP_MAX_HEADER_NAME_BYTES)
		{
			return CK_HTTP_PARSE_BAD_REQUEST;
		}
		for (size_t i = line_start; i < colon; i++)
		{
			if (!is_tchar((unsigned char)parser->buffer[i]))
			{
				return CK_HTTP_PARSE_BAD_REQUEST;
			}
		}

		name.data = parser->buffer + line_start;
		name.length = colon - line_start;
		value.data = parser->buffer + colon + 1;
		value.length = line_end - colon - 1;
		trim_ows(&value);
		if (value.length > CK_HTTP_MAX_HEADER_VALUE_BYTES)
		{
			return CK_HTTP_PARSE_HEADER_TOO_LARGE;
		}
		request->headers[request->header_count].name = name;
		request->headers[request->header_count].value = value;
		request->header_count++;
		line_start = line_end + 2;
	}

	if (!parse_content_length_headers(request, &content_length,
			&has_content_length))
	{
		return CK_HTTP_PARSE_BAD_REQUEST;
	}
	if (!parse_transfer_encoding_headers(request, &has_transfer_encoding,
			&transfer_is_chunked))
	{
		return CK_HTTP_PARSE_BAD_REQUEST;
	}
	if (!parse_connection_headers(request, &connection_close))
	{
		return CK_HTTP_PARSE_BAD_REQUEST;
	}

	if (has_transfer_encoding)
	{
		if (!transfer_is_chunked)
		{
			return CK_HTTP_PARSE_BAD_REQUEST;
		}
		request->body_mode = CK_HTTP_BODY_CHUNKED;
		request->content_length = 0;
		request->connection_close_required = 1;
	}
	else if (has_content_length)
	{
		request->body_mode = CK_HTTP_BODY_CONTENT_LENGTH;
		request->content_length = content_length;
		request->connection_close_required = connection_close;
	}
	else
	{
		request->body_mode = CK_HTTP_BODY_NONE;
		request->content_length = 0;
		request->connection_close_required = connection_close;
	}

	return CK_HTTP_PARSE_COMPLETE;
}

void ck_http_parser_init(ck_http_parser_t *parser)
{
	if (parser == NULL)
	{
		return;
	}
	parser->length = 0;
	parser->complete = 0;
}

ck_http_parse_result_t ck_http_parser_feed(
	ck_http_parser_t *parser,
	const void *data,
	size_t length,
	size_t *consumed,
	ck_http_request_t *request)
{
	const unsigned char *source = data;
	size_t position = 0;
	ck_http_parse_result_t result;

	if (parser == NULL || consumed == NULL || request == NULL
			|| (length != 0 && source == NULL))
	{
		return CK_HTTP_PARSE_BAD_REQUEST;
	}

	*consumed = 0;
	if (parser->complete)
	{
		return CK_HTTP_PARSE_COMPLETE;
	}

	while (position < length)
	{
		if (parser->length >= CK_HTTP_MAX_HEADER_BYTES)
		{
			return CK_HTTP_PARSE_HEADER_TOO_LARGE;
		}

		parser->buffer[parser->length++] = (char)source[position++];
		if (parser->length >= 4U
				&& parser->buffer[parser->length - 4] == '\r'
				&& parser->buffer[parser->length - 3] == '\n'
				&& parser->buffer[parser->length - 2] == '\r'
				&& parser->buffer[parser->length - 1] == '\n')
		{
			result = parse_header_block(parser, parser->length, request);
			if (result != CK_HTTP_PARSE_COMPLETE)
			{
				*consumed = position;
				return result;
			}
			parser->complete = 1;
			*consumed = position;
			return CK_HTTP_PARSE_COMPLETE;
		}
	}

	*consumed = position;
	return CK_HTTP_PARSE_INCOMPLETE;
}
