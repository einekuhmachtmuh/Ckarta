#include "ck_http_chunked.h"

#include <ctype.h>
#include <string.h>

static int is_tchar(unsigned char value)
{
	return (value >= '0' && value <= '9')
			|| (value >= 'A' && value <= 'Z')
			|| (value >= 'a' && value <= 'z')
			|| strchr("!#$%&'*+-.^_`|~", value) != NULL;
}

static int hex_value(unsigned char value)
{
	if (value >= '0' && value <= '9')
	{
		return (int)(value - '0');
	}
	if (value >= 'A' && value <= 'F')
	{
		return (int)(value - 'A') + 10;
	}
	if (value >= 'a' && value <= 'f')
	{
		return (int)(value - 'a') + 10;
	}
	return -1;
}

static int append_line_byte(ck_http_chunked_decoder_t *decoder, unsigned char value)
{
	if (decoder->line_length >= sizeof(decoder->line) - 1U)
	{
		return 0;
	}
	decoder->line[decoder->line_length++] = (char)value;
	return 1;
}

static int finish_line(ck_http_chunked_decoder_t *decoder)
{
	decoder->line[decoder->line_length] = '\0';
	return 1;
}

static int parse_chunk_size(ck_http_chunked_decoder_t *decoder,
	uint64_t *chunk_size)
{
	size_t pos = 0;
	uint64_t value = 0;
	int digits = 0;

	while (pos < decoder->line_length)
	{
		int digit;

		if (decoder->line[pos] == ';')
		{
			break;
		}
		digit = hex_value((unsigned char)decoder->line[pos]);
		if (digit < 0)
		{
			return 0;
		}
		if (value > (UINT64_MAX - (uint64_t)digit) / 16u)
		{
			return 0;
		}
		value = value * 16u + (uint64_t)digit;
		pos++;
		digits = 1;
	}

	if (!digits)
	{
		return 0;
	}

	if (pos < decoder->line_length && decoder->line[pos] == ';')
	{
		for (pos++; pos < decoder->line_length; pos++)
		{
			unsigned char value_byte = (unsigned char)decoder->line[pos];
			if (value_byte == 0x7f
					|| (value_byte < 0x20 && value_byte != '\t'))
			{
				return 0;
			}
		}
	}

	*chunk_size = value;
	return 1;
}

static int valid_trailer_field(const char *line, size_t length)
{
	size_t colon = 0;

	while (colon < length && line[colon] != ':')
	{
		colon++;
	}
	if (colon == 0 || colon == length)
	{
		return 0;
	}
	for (size_t i = 0; i < colon; i++)
	{
		if (!is_tchar((unsigned char)line[i]))
		{
			return 0;
		}
	}
	for (size_t i = colon + 1; i < length; i++)
	{
		unsigned char value = (unsigned char)line[i];
		if (value == 0x7f || (value < 0x20 && value != ' '
				&& value != '\t'))
		{
			return 0;
		}
	}
	return 1;
}

void ck_http_chunked_init(ck_http_chunked_decoder_t *decoder)
{
	if (decoder == NULL)
	{
		return;
	}
	decoder->state = CK_HTTP_CHUNKED_SIZE;
	decoder->chunk_remaining = 0;
	decoder->total_decoded = 0;
	decoder->line_length = 0;
	decoder->trailer_bytes = 0;
	decoder->trailer_count = 0;
}

ck_http_chunked_result_t ck_http_chunked_feed(
	ck_http_chunked_decoder_t *decoder,
	const void *data,
	size_t length,
	size_t *consumed,
	const unsigned char **body_data,
	size_t *body_length)
{
	const unsigned char *source = data;
	size_t position = 0;

	if (decoder == NULL || consumed == NULL || body_data == NULL
			|| body_length == NULL || (length != 0 && source == NULL))
	{
		return CK_HTTP_CHUNKED_BAD_REQUEST;
	}

	*consumed = 0;
	*body_data = NULL;
	*body_length = 0;

	if (decoder->state == CK_HTTP_CHUNKED_DONE)
	{
		return CK_HTTP_CHUNKED_COMPLETE;
	}

	while (position < length)
	{
		if (decoder->state == CK_HTTP_CHUNKED_SIZE)
		{
			unsigned char value = source[position++];

			if (value == '\r')
			{
				if (position >= length)
				{
					position--;
					break;
				}
				if (source[position] != '\n')
				{
					return CK_HTTP_CHUNKED_BAD_REQUEST;
				}
				position++;
				finish_line(decoder);
				if (!parse_chunk_size(decoder, &decoder->chunk_remaining))
				{
					return CK_HTTP_CHUNKED_BAD_REQUEST;
				}
				decoder->line_length = 0;
				decoder->state = decoder->chunk_remaining == 0
						? CK_HTTP_CHUNKED_TRAILERS
						: CK_HTTP_CHUNKED_DATA_STATE;
				continue;
			}

			if (value == '\n')
			{
				return CK_HTTP_CHUNKED_BAD_REQUEST;
			}
			if (!append_line_byte(decoder, value))
			{
				return CK_HTTP_CHUNKED_TOO_LARGE;
			}
			continue;
		}

		if (decoder->state == CK_HTTP_CHUNKED_DATA_STATE)
		{
			size_t available = length - position;
			size_t take = available;

			if (decoder->chunk_remaining < (uint64_t)take)
			{
				take = (size_t)decoder->chunk_remaining;
			}
			if (decoder->total_decoded > CK_HTTP_CHUNKED_MAX_BODY
					|| (uint64_t)take > CK_HTTP_CHUNKED_MAX_BODY
				- decoder->total_decoded)
			{
				return CK_HTTP_CHUNKED_TOO_LARGE;
			}

			*body_data = source + position;
			*body_length = take;
			position += take;
			decoder->chunk_remaining -= (uint64_t)take;
			decoder->total_decoded += (uint64_t)take;
			*consumed = position;
			if (decoder->chunk_remaining == 0)
			{
				decoder->state = CK_HTTP_CHUNKED_DATA_CRLF;
			}
			return CK_HTTP_CHUNKED_DATA;
		}

		if (decoder->state == CK_HTTP_CHUNKED_DATA_CRLF)
		{
			if (length - position < 2)
			{
				break;
			}
			if (source[position] != '\r' || source[position + 1] != '\n')
			{
				return CK_HTTP_CHUNKED_BAD_REQUEST;
			}
			position += 2;
			decoder->state = CK_HTTP_CHUNKED_SIZE;
			continue;
		}

		if (decoder->state == CK_HTTP_CHUNKED_TRAILERS)
		{
			unsigned char value = source[position++];

			if (decoder->trailer_bytes >= CK_HTTP_CHUNKED_MAX_TRAILER_BYTES)
			{
				return CK_HTTP_CHUNKED_TOO_LARGE;
			}
			decoder->trailer_bytes++;
			if (value == '\r')
			{
				if (position >= length)
				{
					position--;
					decoder->trailer_bytes--;
					break;
				}
				if (source[position] != '\n')
				{
					return CK_HTTP_CHUNKED_BAD_REQUEST;
				}
				position++;
				decoder->trailer_bytes++;
				finish_line(decoder);
				if (decoder->line_length == 0)
				{
					decoder->state = CK_HTTP_CHUNKED_DONE;
					*consumed = position;
					return CK_HTTP_CHUNKED_COMPLETE;
				}
				if (decoder->trailer_count >= CK_HTTP_CHUNKED_MAX_TRAILERS
						|| !valid_trailer_field(decoder->line, decoder->line_length))
				{
					return CK_HTTP_CHUNKED_BAD_REQUEST;
				}
				decoder->trailer_count++;
				decoder->line_length = 0;
				continue;
			}
			if (value == '\n')
			{
				return CK_HTTP_CHUNKED_BAD_REQUEST;
			}
			if (!append_line_byte(decoder, value))
			{
				return CK_HTTP_CHUNKED_TOO_LARGE;
			}
			continue;
		}

		return CK_HTTP_CHUNKED_BAD_REQUEST;
	}

	*consumed = position;
	return CK_HTTP_CHUNKED_INCOMPLETE;
}

ck_http_chunked_state_t ck_http_chunked_state(
	const ck_http_chunked_decoder_t *decoder)
{
	return decoder == NULL ? CK_HTTP_CHUNKED_STATE_INVALID : decoder->state;
}

uint64_t ck_http_chunked_total_decoded(
	const ck_http_chunked_decoder_t *decoder)
{
	return decoder == NULL ? 0 : decoder->total_decoded;
}
