#include "ck_http_input.h"

#include <string.h>

void ck_http_input_init(ck_http_input_t *input)
{
	if (input == NULL)
	{
		return;
	}
	memset(input, 0, sizeof(*input));
	ck_http_parser_init(&input->parser);
	ck_http_chunked_init(&input->chunked);
}

static ck_http_input_result_t map_chunked_result(ck_http_chunked_result_t result)
{
	switch (result)
	{
	case CK_HTTP_CHUNKED_INCOMPLETE:
		return CK_HTTP_INPUT_INCOMPLETE;
	case CK_HTTP_CHUNKED_DATA:
		return CK_HTTP_INPUT_BODY;
	case CK_HTTP_CHUNKED_COMPLETE:
		return CK_HTTP_INPUT_COMPLETE;
	case CK_HTTP_CHUNKED_TOO_LARGE:
		return CK_HTTP_INPUT_TOO_LARGE;
	case CK_HTTP_CHUNKED_BAD_REQUEST:
	default:
		return CK_HTTP_INPUT_BAD_REQUEST;
	}
}

ck_http_input_result_t ck_http_input_feed(
	ck_http_input_t *input,
	const void *data,
	size_t length,
	size_t *consumed,
	const unsigned char **body_data,
	size_t *body_length)
{
	const unsigned char *source = data;
	size_t header_consumed = 0;
	ck_http_parse_result_t parse_result;

	if (input == NULL || consumed == NULL || body_data == NULL
			|| body_length == NULL || (length != 0 && source == NULL))
	{
		return CK_HTTP_INPUT_BAD_REQUEST;
	}

	*consumed = 0;
	*body_data = NULL;
	*body_length = 0;

	if (input->pending_body_length != 0)
	{
		*body_data = input->pending_body_data;
		*body_length = input->pending_body_length;
		return CK_HTTP_INPUT_BODY;
	}

	if (input->message_complete)
	{
		return CK_HTTP_INPUT_COMPLETE;
	}

	if (!input->header_complete)
	{
		parse_result = ck_http_parser_feed(&input->parser, data, length,
				&header_consumed, &input->request);
		*consumed = header_consumed;
		if (parse_result == CK_HTTP_PARSE_INCOMPLETE)
		{
			return CK_HTTP_INPUT_INCOMPLETE;
		}
		if (parse_result == CK_HTTP_PARSE_HEADER_TOO_LARGE)
		{
			return CK_HTTP_INPUT_TOO_LARGE;
		}
		if (parse_result != CK_HTTP_PARSE_COMPLETE)
		{
			return CK_HTTP_INPUT_BAD_REQUEST;
		}
		input->header_complete = 1;
		input->content_length_remaining = input->request.content_length;
		if (input->request.body_mode == CK_HTTP_BODY_CHUNKED)
		{
			ck_http_chunked_init(&input->chunked);
		}
		if (input->request.body_mode == CK_HTTP_BODY_NONE
				|| (input->request.body_mode == CK_HTTP_BODY_CONTENT_LENGTH
					&& input->content_length_remaining == 0))
		{
			input->message_complete = 1;
			return CK_HTTP_INPUT_COMPLETE;
		}
	}

	if (input->request.body_mode == CK_HTTP_BODY_CONTENT_LENGTH)
	{
		size_t remaining_input = length - *consumed;
		size_t take = remaining_input;

		if (input->content_length_remaining < (uint64_t)take)
		{
			take = (size_t)input->content_length_remaining;
		}
		*body_data = source + *consumed;
		*body_length = take;
		input->pending_body_data = *body_data;
		input->pending_body_length = take;
		input->pending_input_consumed = take;
		input->pending_message_complete =
				input->content_length_remaining == (uint64_t)take;
		input->content_length_remaining -= (uint64_t)take;
		if (take != 0)
		{
			return CK_HTTP_INPUT_BODY;
		}
		return CK_HTTP_INPUT_INCOMPLETE;
	}

	if (input->request.body_mode == CK_HTTP_BODY_CHUNKED)
	{
		size_t chunk_consumed = 0;
		ck_http_chunked_result_t chunk_result;

		chunk_result = ck_http_chunked_feed(&input->chunked,
				source + *consumed, length - *consumed, &chunk_consumed,
				body_data, body_length);
		if (*body_length != 0)
		{
			input->pending_body_data = *body_data;
			input->pending_body_length = *body_length;
			input->pending_input_consumed = chunk_consumed;
			input->pending_message_complete =
					chunk_result == CK_HTTP_CHUNKED_COMPLETE;
			return CK_HTTP_INPUT_BODY;
		}
		*consumed += chunk_consumed;
		if (chunk_result == CK_HTTP_CHUNKED_COMPLETE)
		{
			input->message_complete = 1;
		}
		return map_chunked_result(chunk_result);
	}

	return CK_HTTP_INPUT_BAD_REQUEST;
}

int ck_http_input_ack_body(
	ck_http_input_t *input,
	size_t *consumed)
{
	size_t value;

	if (input == NULL || consumed == NULL
			|| input->pending_body_length == 0)
	{
		return -1;
	}

	value = input->pending_input_consumed;
	if (input->pending_message_complete)
	{
		input->message_complete = 1;
	}
	input->pending_body_data = NULL;
	input->pending_body_length = 0;
	input->pending_input_consumed = 0;
	input->pending_message_complete = 0;
	*consumed = value;
	return 0;
}

ck_http_input_result_t ck_http_input_eof(
	ck_http_input_t *input,
	const unsigned char **body_data,
	size_t *body_length)
{
	if (input == NULL || body_data == NULL || body_length == NULL)
	{
		return CK_HTTP_INPUT_BAD_REQUEST;
	}
	*body_data = NULL;
	*body_length = 0;
	if (input->message_complete)
	{
		return CK_HTTP_INPUT_COMPLETE;
	}
	if (!input->header_complete)
	{
		return CK_HTTP_INPUT_EOF_INCOMPLETE;
	}
	return CK_HTTP_INPUT_EOF_INCOMPLETE;
}

const ck_http_request_t *ck_http_input_request(const ck_http_input_t *input)
{
	return input == NULL || !input->header_complete ? NULL : &input->request;
}

int ck_http_input_complete(const ck_http_input_t *input)
{
	return input != NULL && input->message_complete;
}

void ck_http_input_next_request(ck_http_input_t *input)
{
	if (input == NULL)
	{
		return;
	}
	memset(input, 0, sizeof(*input));
	ck_http_parser_init(&input->parser);
	ck_http_chunked_init(&input->chunked);
}
