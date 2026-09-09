#include "ck_request.h"

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

	if (descriptor->body == NULL
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

int ck_request_init(ck_request_t *request,
		const ck_request_descriptor_t *descriptor)
{
	if (request == NULL || !ck_request_descriptor_valid(descriptor))
	{
		return -1;
	}

	request->descriptor = *descriptor;
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
				|| current == CK_REQUEST_COMPLETED
				|| current == CK_REQUEST_FAILED)
		{
			return 0;
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

int ck_request_finish(ck_request_t *request, ck_request_state_t terminal_state)
{
	uint32_t expected = CK_REQUEST_RUNNING;

	if (request == NULL
			|| (terminal_state != CK_REQUEST_COMPLETED
					&& terminal_state != CK_REQUEST_FAILED))
	{
		return -1;
	}

	if (atomic_compare_exchange_strong_explicit(
			&request->state, &expected, (uint32_t)terminal_state,
			memory_order_acq_rel, memory_order_acquire))
	{
		return 0;
	}

	if (expected == CK_REQUEST_CANCELLING
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
		return CK_REQUEST_FAILED;
	}

	return (ck_request_state_t)atomic_load_explicit(
			&request->state, memory_order_acquire);
}
