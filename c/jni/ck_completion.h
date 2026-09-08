#ifndef CKARTA_COMPLETION_H
#define CKARTA_COMPLETION_H

#include <stdatomic.h>
#include <stdint.h>

typedef enum ck_completion_state
{
	CK_COMPLETION_PENDING = 0,
	CK_COMPLETION_PUBLISHED = 1,
	CK_COMPLETION_CONSUMED = 2
} ck_completion_state_t;

typedef struct ck_completion
{
	_Atomic uint32_t state;
	_Atomic int64_t result;
	_Atomic int32_t status;
} ck_completion_t;

void ck_completion_init(ck_completion_t *completion);
int ck_completion_publish(ck_completion_t *completion, int64_t result, int32_t status);
int ck_completion_poll(ck_completion_t *completion, int64_t *result, int32_t *status);

#endif
