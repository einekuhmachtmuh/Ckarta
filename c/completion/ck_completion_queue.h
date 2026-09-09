#ifndef CKARTA_COMPLETION_QUEUE_H
#define CKARTA_COMPLETION_QUEUE_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#include "../event/ck_completion_notification.h"

#define CK_COMPLETION_QUEUE_ABI_VERSION 1u
#define CK_COMPLETION_QUEUE_CAPACITY 64u

typedef struct ck_completion_record
{
	uint64_t request_id;
	uint64_t owner_token;
	uint64_t lifetime_token;
	int64_t result;
	int32_t status;
} ck_completion_record_t;

typedef struct ck_completion_queue
{
	pthread_mutex_t lock;
	ck_completion_record_t entries[CK_COMPLETION_QUEUE_CAPACITY];
	size_t head;
	size_t tail;
	size_t count;
	ck_completion_notification_t notification;
	int initialized;
	int closed;
} ck_completion_queue_t;

int ck_completion_queue_init(ck_completion_queue_t *queue);
int ck_completion_queue_push(ck_completion_queue_t *queue,
		const ck_completion_record_t *record);
int ck_completion_queue_pop(ck_completion_queue_t *queue,
		ck_completion_record_t *record);
int ck_completion_queue_notify_fd(const ck_completion_queue_t *queue);
int ck_completion_queue_drain_notification(ck_completion_queue_t *queue);
int ck_completion_queue_close(ck_completion_queue_t *queue);
void ck_completion_queue_destroy(ck_completion_queue_t *queue);

#endif
