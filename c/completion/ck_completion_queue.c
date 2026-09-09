#include "ck_completion_queue.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

static int ck_completion_queue_lock(ck_completion_queue_t *queue)
{
	return pthread_mutex_lock(&queue->lock);
}

int ck_completion_queue_init(ck_completion_queue_t *queue)
{
	int result;

	if (queue == NULL)
	{
		return -1;
	}

	memset(queue, 0, sizeof(*queue));
	queue->event_fd = -1;

	result = pthread_mutex_init(&queue->lock, NULL);
	if (result != 0)
	{
		return result;
	}

	queue->event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (queue->event_fd < 0)
	{
		result = errno;
		(void)pthread_mutex_destroy(&queue->lock);
		return result;
	}

	queue->initialized = 1;
	return 0;
}

int ck_completion_queue_push(ck_completion_queue_t *queue,
		const ck_completion_record_t *record)
{
	int result;
	uint64_t signal_value = 1;

	if (queue == NULL || record == NULL || !queue->initialized)
	{
		return -1;
	}

	result = ck_completion_queue_lock(queue);
	if (result != 0)
	{
		return result;
	}

	if (queue->closed)
	{
		(void)pthread_mutex_unlock(&queue->lock);
		return 2;
	}

	if (queue->count == CK_COMPLETION_QUEUE_CAPACITY)
	{
		(void)pthread_mutex_unlock(&queue->lock);
		return 1;
	}

	queue->entries[queue->tail] = *record;
	queue->tail = (queue->tail + 1U) % CK_COMPLETION_QUEUE_CAPACITY;
	queue->count++;

	result = pthread_mutex_unlock(&queue->lock);
	if (result != 0)
	{
		return result;
	}

	if (write(queue->event_fd, &signal_value, sizeof(signal_value)) < 0)
	{
		if (errno == EAGAIN)
		{
			return 0;
		}
		return errno;
	}

	return 0;
}

int ck_completion_queue_pop(ck_completion_queue_t *queue,
		ck_completion_record_t *record)
{
	int result;

	if (queue == NULL || record == NULL || !queue->initialized)
	{
		return -1;
	}

	result = ck_completion_queue_lock(queue);
	if (result != 0)
	{
		return result;
	}

	if (queue->count == 0U)
	{
		(void)pthread_mutex_unlock(&queue->lock);
		return 0;
	}

	*record = queue->entries[queue->head];
	queue->head = (queue->head + 1U) % CK_COMPLETION_QUEUE_CAPACITY;
	queue->count--;

	result = pthread_mutex_unlock(&queue->lock);
	return result == 0 ? 1 : result;
}

int ck_completion_queue_notify_fd(const ck_completion_queue_t *queue)
{
	if (queue == NULL || !queue->initialized)
	{
		return -1;
	}

	return queue->event_fd;
}

int ck_completion_queue_drain_notification(ck_completion_queue_t *queue)
{
	uint64_t value;
	ssize_t result;

	if (queue == NULL || !queue->initialized)
	{
		return -1;
	}

	for (;;)
	{
		result = read(queue->event_fd, &value, sizeof(value));
		if (result == (ssize_t)sizeof(value))
		{
			continue;
		}

		if (result < 0 && errno == EAGAIN)
		{
			return 0;
		}

		return result < 0 ? errno : EIO;
	}
}

int ck_completion_queue_close(ck_completion_queue_t *queue)
{
	int result;

	if (queue == NULL || !queue->initialized)
	{
		return -1;
	}

	result = ck_completion_queue_lock(queue);
	if (result != 0)
	{
		return result;
	}

	if (queue->closed)
	{
		(void)pthread_mutex_unlock(&queue->lock);
		return 1;
	}

	queue->closed = 1;
	return pthread_mutex_unlock(&queue->lock);
}

void ck_completion_queue_destroy(ck_completion_queue_t *queue)
{
	if (queue == NULL || !queue->initialized)
	{
		return;
	}

	(void)ck_completion_queue_close(queue);
	if (queue->event_fd >= 0)
	{
		(void)close(queue->event_fd);
		queue->event_fd = -1;
	}
	(void)pthread_mutex_destroy(&queue->lock);
	queue->initialized = 0;
}
