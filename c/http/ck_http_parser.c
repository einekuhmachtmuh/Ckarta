#include "ck_http_parser.h"

#include <ctype.h>
#include <stdint.h>
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
		uint64_t next;

		if (digit < '0' || digit > '9')
		{
			return 0;
		}
		if (result > (UINT64_MAX - (uint64_t)(digit - '0')) / 10u)
		{
			return 0;
		}
		next = result * 10u + (uint64_t)(digit - '0');
		result = next;
	}

	*value = result;
	return 1;
}

static int find_line_end(const char *buffer, size_t length,
	size_t start, size_t *line_end)
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
	size_t first = 0;
	size_t second = 0;
	size_t pos;

	while (first < line_end && buffer[first] != ' ' && buffer[first] != '\t')
	{
		first++;
	}
	if (first == 0 || first >= line_end ||
			first > CK_HTTP_MAX_METHOD_BYTES)
	{
		return 0;
	}

	for (pos = 0; pos < first; pos++)
	{
		if (!is_tchar((unsigned char)buffer[pos]))
		{
			return 0;
		}
	}

	pos = first;
	while (pos < line_end && (buffer[pos] == ' ' || buffer[pos] == '\t'))
	{
		pos++;
	}
	second = pos;
	while (second < line_end && buffer[second] != ' '
			&& buffer[second] != '\t')
	{
		second++;
	}
	if (second == pos || second - pos > CK_HTTP_MAX_TARGET_BYTES)
	{
		return 0;
	}

	request->method.data = buffer;
	request->method.length = first;
	request->target.data = buffer + pos;
	request->target.length = second - pos;

	for (size_t i = 0; i < request->target.length; i++)
	{
		unsigned char value = (unsigned char)request->target.data[i];
		if (value <= 0x20 || value == 0x7f)
		{
			return 0;
		}
	}

	pos = second;
	while (pos < line_end && (buffer[pos] == ' ' || buffer[pos] == '\t'))
	{
		pos++;
	}
	if (pos >= line_end || line_end - pos > CK_HTTP_MAX_VERSION_BYTES)
	{
		return 0;
	}

	request->version.data = buffer + pos;
	request->version.length = line_end - pos;
	if (request->version.length != sizeof("HTTP/1.1") - 1 ||
			memcmp(request->version.data, "HTTP/1.1", sizeof("HTTP/1.1") - 1) != 0)
	{
		return 0;
	}

	return 1;
}

static int parse_content_length_headers(const ck_http_request_t *request,
	uint64_t *value, int *present)
{
	uint64_t canonical = 0;
	int found = 0;

	for (size_t i = 0; i < request->header_count; i++)
	{
		ck_http_span_t value_span;
		size_t start;

		if (!span_equal_ci(request->headers[i].name, "Content-Length"))
		{
			continue;
		}

		value_span = request->headers[i].value;
		start = 0;
		while (start < value_span.length)
		{
			size_t end = start;
			ck_http_span_t item;
			uint64_t parsed;

			while (start < value_span.length && (value_span.data[start] == ' '
					|| value_span.data[start] == '\t'))
			{
				start++;
			}
			end = start;
			while (end < value_span.length && value_span.data[end] != ',')
			{
				end++;
			}
			item.data = value_span.data + start;
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

			start = end;
			if (start < value_span.length)
			{
				start++;
			}
		}
	}

	*value = found ? canonical : 0;
	*present = found;
	return 1;
}

static int parse_transfer_encoding_headers(const ck_http_request_t *request,
	int *present, int *final_chunked)
{
	int found = 0;
	int final_is_chunked = 0;

	for (size_t i = 0; i < request->header_count; i++)
	{
		ck_http_span_t value_span;
		size_t start = 0;

		if (!span_equal_ci(request->headers[i].name, "Transfer-Encoding"))
		{
			continue;
		}
		found = 1;
		value_span = request->headers[i].value;
		while (start < value_span.length)
		{
			size_t end = start;
			ck_http_span_t token;

			while (start < value_span.length && (value_span.data[start] == ' '
					|| value_span.data[start] == '\t'
					|| value_span.data[start] == ','))
			{
				start++;
			}
			if (start == value_span.length)
			{
				return 0;
			}
			end = start;
			while (end < value_span.length && value_span.data[end] != ',')
			{
				end++;
			}
			token.data = value_span.data + start;
			token.length = end - start;
			trim_ows(&token);
			if (token.length == 0)
			{
				return 0;
			}
			for (size_t j = 0; j < token.length; j++)
			{
				if (!is_tchar((unsigned char)token.data[j]))
				{
					return 0;
				}
			}
			final_is_chunked = span_equal_ci(token, "chunked");

			start = end;
			if (start < value_span.length)
			{
				start++;
			}
		}
	}

	*present = found;
	*final_chunked = final_is_chunked;
	return found;
}

static ck_http_parse_result_t parse_complete_header_block(
	ck_http_parser_t *parser, size_t header_end, ck_http_request_t *request)
{
	size_t line_start = 0;
	size_t line_end;
	uint64_t content_length;
	int has_content_length;
	int has_transfer_encoding;
	int final_chunked;

	memset(request, 0, sizeof(*request));
	if (!find_line_end(parser->buffer, header_end, 0, &line_end)
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

		if (!find_line_end(parser->buffer, header_end, line_start, &line_end))
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

	if (!parse_content_length_headers(request, &content_length, &has_content_length))
	{
		return CK_HTTP_PARSE_BAD_REQUEST;
	}

	if (!parse_transfer_encoding_headers(request, &has_transfer_encoding,
			&final_chunked) && has_transfer_encoding)
	{
		return CK_HTTP_PARSE_BAD_REQUEST;
	}

	if (has_transfer_encoding)
	{
		if (!final_chunked)
		{
			return CK_HTTP_PARSE_BAD_REQUEST;
		}
		request->body_mode = CK_HTTP_BODY_CHUNKED;
		request->content_length = 0;
		request->connection_close_required = has_content_length;
	}
	else if (has_content_length)
	{
		request->body_mode = CK_HTTP_BODY_CONTENT_LENGTH;
		request->content_length = content_length;
		request->connection_close_required = 0;
	}
	else
	{
		request->body_mode = CK_HTTP_BODY_NONE;
		request->content_length = 0;
		request->connection_close_required = 0;
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
	const char *source = data;
	size_t header_end = 0;
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
	if (length > CK_HTTP_MAX_HEADER_BYTES - parser->length)
	{
		return CK_HTTP_PARSE_HEADER_TOO_LARGE;
	}
	if (length != 0)
	{
		memcpy(parser->buffer + parser->length, source, length);
		parser->length += length;
	}

	for (size_t i = 3; i < parser->length; i++)
	{
		if (parser->buffer[i - 3] == '\r'
				&& parser->buffer[i - 2] == '\n'
				&& parser->buffer[i - 1] == '\r'
				&& parser->buffer[i] == '\n')
		{
			header_end = i + 1;
			break;
		}
	}

	if (header_end == 0)
	{
		return CK_HTTP_PARSE_INCOMPLETE;
	}

	result = parse_complete_header_block(parser, header_end, request);
	if (result != CK_HTTP_PARSE_COMPLETE)
	{
		return result;
	}

	parser->complete = 1;
	*consumed = header_end;
	return CK_HTTP_PARSE_COMPLETE;
}
