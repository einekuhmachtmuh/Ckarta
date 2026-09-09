#include "ck_request.h"

#include <limits.h>
#include <string.h>

#include <stddef.h>
#include <stdint.h>

static int ck_request_descriptor_valid(const ck_request_descriptor_t *descriptor)
{
	if (descriptor == NULL)
	{
		return 0;
	}

	if (descriptor->abi_version != CK_JNI_ABI_VERSION)
	{
		return 0;
	}

	if (descriptor->struct_size < sizeof(ck_request_descriptor_t))
	{
		return 0;
	}

	if ((descriptor->metadata == NULL && descriptor->metadata_length != 0)
			|| descriptor->metadata_length > (uint64_t)INT32_MAX
			|| ((descriptor->feature_flags & CK_REQUEST_FEATURE_METADATA_BUFFER) != 0
				&& descriptor->metadata_length == 0)
			|| ((descriptor->feature_flags & CK_REQUEST_FEATURE_METADATA_BUFFER) == 0
				&& descriptor->metadata_length != 0)
			|| (descriptor->body == NULL && descriptor->body_length != 0)
			|| descriptor->body_length > (uint64_t)INT32_MAX)
	{
		return 0;
	}

	if (descriptor->owner_token > (uint64_t)INT64_MAX
			|| descriptor->lifetime_token > (uint64_t)INT64_MAX
			|| descriptor->request_id > (uint64_t)INT64_MAX
			|| descriptor->request_id == 0)
	{
		return 0;
	}

	return 1;
}


static void write_u32_be(unsigned char *output, uint32_t value)
{
	output[0] = (unsigned char)(value >> 24);
	output[1] = (unsigned char)(value >> 16);
	output[2] = (unsigned char)(value >> 8);
	output[3] = (unsigned char)value;
}

int ck_request_init_http(ck_request_t *request,
	const ck_http_request_t *http_request,
	const unsigned char *body,
	size_t body_length,
	uint64_t request_id,
	uint64_t owner_token,
	uint64_t lifetime_token)
{
	ck_request_descriptor_t descriptor;
	size_t offset = 12U;
	size_t metadata_length;

	if (request == NULL || http_request == NULL
			|| http_request->method.data == NULL
			|| http_request->target.data == NULL
			|| http_request->version.data == NULL
			|| http_request->method.length > CK_HTTP_MAX_METHOD_BYTES
			|| http_request->target.length > CK_HTTP_MAX_TARGET_BYTES
			|| http_request->version.length > CK_HTTP_MAX_VERSION_BYTES
			|| (body == NULL && body_length != 0)
			|| body_length > (size_t)INT32_MAX)
	{
		return -1;
	}

	metadata_length = offset + http_request->method.length
			+ http_request->target.length + http_request->version.length + 1U;
	if (metadata_length > sizeof(request->metadata_storage)
			|| metadata_length > (size_t)INT32_MAX)
	{
		return -1;
	}

	memset(request, 0, sizeof(*request));
	write_u32_be(request->metadata_storage,
			(uint32_t)http_request->method.length);
	write_u32_be(request->metadata_storage + 4U,
			(uint32_t)http_request->target.length);
	write_u32_be(request->metadata_storage + 8U,
			(uint32_t)http_request->version.length);

	memcpy(request->metadata_storage + offset,
			http_request->method.data, http_request->method.length);
	offset += http_request->method.length;
	memcpy(request->metadata_storage + offset,
			http_request->target.data, http_request->target.length);
	offset += http_request->target.length;
	memcpy(request->metadata_storage + offset,
			http_request->version.data, http_request->version.length);
	offset += http_request->version.length;
	request->metadata_storage[offset] =
			(unsigned char)http_request->connection_close_required;

	memset(&descriptor, 0, sizeof(descriptor));
	descriptor.abi_version = CK_JNI_ABI_VERSION;
	descriptor.struct_size = sizeof(descriptor);
	descriptor.feature_flags =
			CK_REQUEST_FEATURE_DIRECT_BUFFER
			| CK_REQUEST_FEATURE_METADATA_BUFFER;
	descriptor.ownership_flags =
			CK_REQUEST_OWNS_NATIVE_STORAGE
			| CK_REQUEST_JAVA_BORROWS_BUFFER;
	descriptor.owner_token = owner_token;
	descriptor.lifetime_token = lifetime_token;
	descriptor.request_id = request_id;
	descriptor.metadata = request->metadata_storage;
	descriptor.metadata_length = (uint64_t)metadata_length;
	descriptor.body = body;
	descriptor.body_length = (uint64_t)body_length;

	return ck_request_init(request, &descriptor);
}

int ck_request_init(ck_request_t *request,
		const ck_request_descriptor_t *descriptor)
{
	if (request == NULL || !ck_request_descriptor_valid(descriptor))
	{
		return -1;
	}

	request->descriptor = *descriptor;
	ck_error_init(&request->error);
	atomic_init(&request->state, CK_REQUEST_PENDING);
	return 0;
}

int ck_request_begin(ck_request_t *request)
{
	uint32_t expected = CK_REQUEST_PENDING;

	if (request == NULL)
	{
		return -1;
	}

	return atomic_compare_exchange_strong_explicit(
			&request->state, &expected, CK_REQUEST_RUNNING,
			memory_order_acq_rel, memory_order_acquire) ? 0 : 1;
}

int ck_request_cancel(ck_request_t *request)
{
	uint32_t current;

	if (request == NULL)
	{
		return -1;
	}

	for (;;)
	{
		current = atomic_load_explicit(&request->state, memory_order_acquire);

		if (current == CK_REQUEST_CANCELLING
			|| current == CK_REQUEST_FAILING
			|| current == CK_REQUEST_COMPLETED
			|| current == CK_REQUEST_FAILED)
		{
			return 1;
		}

		if (current != CK_REQUEST_PENDING && current != CK_REQUEST_RUNNING)
		{
			return -1;
		}

		if (atomic_compare_exchange_weak_explicit(
				&request->state, &current, CK_REQUEST_CANCELLING,
				memory_order_acq_rel, memory_order_acquire))
		{
			return 0;
		}
	}
}

int ck_request_fail(ck_request_t *request, const ck_error_t *error)
{
	uint32_t expected = CK_REQUEST_RUNNING;

	if (request == NULL || error == NULL
			|| error->abi_version != CK_ERROR_ABI_VERSION)
	{
		return -1;
	}

	if (!atomic_compare_exchange_strong_explicit(
			&request->state, &expected, CK_REQUEST_FAILING,
			memory_order_acq_rel, memory_order_acquire))
	{
		if (expected == CK_REQUEST_CANCELLING
				|| expected == CK_REQUEST_COMPLETED
				|| expected == CK_REQUEST_FAILED
				|| expected == CK_REQUEST_FAILING)
		{
			return 1;
		}

		return -1;
	}

	request->error = *error;
	atomic_store_explicit(&request->state, CK_REQUEST_FAILED,
			memory_order_release);
	return 0;
}

const ck_error_t *ck_request_error(const ck_request_t *request)
{
	if (request == NULL)
	{
		return NULL;
	}

	if (atomic_load_explicit(&request->state, memory_order_acquire)
			!= CK_REQUEST_FAILED)
	{
		return NULL;
	}

	return &request->error;
}

int ck_request_finish(ck_request_t *request)
{
	uint32_t expected = CK_REQUEST_RUNNING;

	if (request == NULL)
	{
		return -1;
	}

	if (atomic_compare_exchange_strong_explicit(
			&request->state, &expected, CK_REQUEST_COMPLETED,
			memory_order_acq_rel, memory_order_acquire))
	{
		return 0;
	}

	if (expected == CK_REQUEST_CANCELLING
			|| expected == CK_REQUEST_FAILING
			|| expected == CK_REQUEST_COMPLETED
			|| expected == CK_REQUEST_FAILED)
	{
		return 1;
	}

	return -1;
}

ck_request_state_t ck_request_state(const ck_request_t *request)
{
	if (request == NULL)
	{
		return CK_REQUEST_STATE_INVALID;
	}

	return (ck_request_state_t)atomic_load_explicit(
			&request->state, memory_order_acquire);
}
