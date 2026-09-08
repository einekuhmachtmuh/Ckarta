#include "ck_completion.h"

#include <stddef.h>

void ck_completion_init(ck_completion_t *completion)
{
	if (completion == NULL)
	{
		return;
	}

	atomic_init(&completion->state, CK_COMPLETION_PENDING);
	atomic_init(&completion->result, 0);
	atomic_init(&completion->status, 0);
}

int ck_completion_publish(ck_completion_t *completion, int64_t result, int32_t status)
{
	uint32_t expected = CK_COMPLETION_PENDING;

	if (completion == NULL)
	{
		return -1;
	}

	atomic_store_explicit(&completion->result, result, memory_order_relaxed);
	atomic_store_explicit(&completion->status, status, memory_order_relaxed);

	return atomic_compare_exchange_strong_explicit(
			&completion->state, &expected, CK_COMPLETION_PUBLISHED,
			memory_order_release, memory_order_acquire) ? 0 : 1;
}

int ck_completion_poll(ck_completion_t *completion, int64_t *result, int32_t *status)
{
	uint32_t expected = CK_COMPLETION_PUBLISHED;

	if (completion == NULL || result == NULL || status == NULL)
	{
		return -1;
	}

	if (!atomic_compare_exchange_strong_explicit(
			&completion->state, &expected, CK_COMPLETION_CONSUMED,
			memory_order_acq_rel, memory_order_acquire))
	{
		return 0;
	}

	*result = atomic_load_explicit(&completion->result, memory_order_relaxed);
	*status = atomic_load_explicit(&completion->status, memory_order_relaxed);
	return 1;
}
