#include "ck_http_request_body.h"

#include <string.h>

static uint64_t body_used(uint64_t head, uint64_t tail)
{
	return head - tail;
}

void ck_http_request_body_init(ck_http_request_body_t *body)
{
	if (body == NULL)
	{
		return;
	}

	atomic_init(&body->head, 0);
	atomic_init(&body->tail, 0);
	atomic_init(&body->eof, 0);
	atomic_init(&body->error, 0);
}

ck_http_request_body_write_result_t ck_http_request_body_write(
	ck_http_request_body_t *body,
	const unsigned char *data,
	size_t length,
	size_t *written)
{
	uint64_t head;
	uint64_t tail;
	uint64_t used;
	size_t available;
	size_t first;

	if (body == NULL || written == NULL || (length != 0 && data == NULL))
	{
		return CK_HTTP_REQUEST_BODY_WRITE_INVALID;
	}

	*written = 0;
	if (length == 0)
	{
		return CK_HTTP_REQUEST_BODY_WRITE_OK;
	}

	if (atomic_load_explicit(&body->eof, memory_order_acquire)
			|| atomic_load_explicit(&body->error, memory_order_acquire))
	{
		return CK_HTTP_REQUEST_BODY_WRITE_CLOSED;
	}

	head = atomic_load_explicit(&body->head, memory_order_relaxed);
	tail = atomic_load_explicit(&body->tail, memory_order_acquire);
	used = body_used(head, tail);
	if (used > CK_HTTP_REQUEST_BODY_BUFFER_BYTES)
	{
		return CK_HTTP_REQUEST_BODY_WRITE_INVALID;
	}
	available = CK_HTTP_REQUEST_BODY_BUFFER_BYTES - (size_t)used;
	if (available < length)
	{
		return CK_HTTP_REQUEST_BODY_WRITE_WOULD_BLOCK;
	}

	first = CK_HTTP_REQUEST_BODY_BUFFER_BYTES
			- (size_t)(head % CK_HTTP_REQUEST_BODY_BUFFER_BYTES);
	if (first > length)
	{
		first = length;
	}

	memcpy(body->buffer + (head % CK_HTTP_REQUEST_BODY_BUFFER_BYTES),
			data, first);
	if (length > first)
	{
		memcpy(body->buffer, data + first, length - first);
	}

	atomic_store_explicit(&body->head, head + length, memory_order_release);
	*written = length;
	return CK_HTTP_REQUEST_BODY_WRITE_OK;
}

ck_http_request_body_read_result_t ck_http_request_body_read(
	ck_http_request_body_t *body,
	unsigned char *data,
	size_t capacity,
	size_t *read)
{
	uint64_t head;
	uint64_t tail;
	uint64_t available;
	size_t length;
	size_t first;

	if (body == NULL || read == NULL || (capacity != 0 && data == NULL))
	{
		return CK_HTTP_REQUEST_BODY_READ_INVALID;
	}

	*read = 0;
	head = atomic_load_explicit(&body->head, memory_order_acquire);
	tail = atomic_load_explicit(&body->tail, memory_order_relaxed);
	available = body_used(head, tail);
	if (available > CK_HTTP_REQUEST_BODY_BUFFER_BYTES)
	{
		return CK_HTTP_REQUEST_BODY_READ_INVALID;
	}

	if (available == 0)
	{
		if (atomic_load_explicit(&body->error, memory_order_acquire))
		{
			return CK_HTTP_REQUEST_BODY_READ_ERROR;
		}
		if (atomic_load_explicit(&body->eof, memory_order_acquire))
		{
			return CK_HTTP_REQUEST_BODY_READ_EOF;
		}
		return CK_HTTP_REQUEST_BODY_READ_WOULD_BLOCK;
	}

	if (capacity == 0)
	{
		return CK_HTTP_REQUEST_BODY_READ_WOULD_BLOCK;
	}

	length = (size_t)available;
	if (length > capacity)
	{
		length = capacity;
	}

	first = CK_HTTP_REQUEST_BODY_BUFFER_BYTES
			- (size_t)(tail % CK_HTTP_REQUEST_BODY_BUFFER_BYTES);
	if (first > length)
	{
		first = length;
	}

	memcpy(data,
			body->buffer + (tail % CK_HTTP_REQUEST_BODY_BUFFER_BYTES),
			first);
	if (length > first)
	{
		memcpy(data + first, body->buffer, length - first);
	}

	atomic_store_explicit(&body->tail, tail + length, memory_order_release);
	*read = length;
	return CK_HTTP_REQUEST_BODY_READ_DATA;
}

void ck_http_request_body_mark_eof(ck_http_request_body_t *body)
{
	if (body == NULL)
	{
		return;
	}

	atomic_store_explicit(&body->eof, 1, memory_order_release);
}

void ck_http_request_body_mark_error(ck_http_request_body_t *body)
{
	if (body == NULL)
	{
		return;
	}

	atomic_store_explicit(&body->error, 1, memory_order_release);
}

size_t ck_http_request_body_available(const ck_http_request_body_t *body)
{
	uint64_t head;
	uint64_t tail;
	uint64_t available;

	if (body == NULL)
	{
		return 0;
	}

	head = atomic_load_explicit(&body->head, memory_order_acquire);
	tail = atomic_load_explicit(&body->tail, memory_order_acquire);
	available = body_used(head, tail);
	if (available > CK_HTTP_REQUEST_BODY_BUFFER_BYTES)
	{
		return 0;
	}
	return (size_t)available;
}

int ck_http_request_body_is_finished(const ck_http_request_body_t *body)
{
	if (body == NULL)
	{
		return 0;
	}

	return atomic_load_explicit(&body->eof, memory_order_acquire)
			&& ck_http_request_body_available(body) == 0;
}
