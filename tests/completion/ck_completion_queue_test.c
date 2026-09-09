#include "../../c/completion/ck_completion_queue.h"

#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

typedef struct
{
	ck_completion_queue_t *queue;
	uint64_t base;
	int result;
} producer_args_t;

static void *producer_main(void *arg)
{
	producer_args_t *args = arg;
	ck_completion_record_t record;
	uint64_t i;

	for (i = 0; i < 8; i++)
	{
		record.request_id = args->base + i;
		record.owner_token = 1000 + i;
		record.lifetime_token = 2000 + i;
		record.result = (int64_t)i;
		record.status = 0;
		if (ck_completion_queue_push(args->queue, &record) != 0)
		{
			args->result = -1;
			return NULL;
		}
	}

	args->result = 0;
	return NULL;
}

int main(void)
{
	ck_completion_queue_t queue;
	ck_completion_record_t record;
	producer_args_t left = { 0 };
	producer_args_t right = { 0 };
	pthread_t left_thread;
	pthread_t right_thread;
	struct epoll_event event;
	int epoll_fd;
	int notification_fd;
	int result;
	int count = 0;

	assert(ck_completion_queue_init(&queue) == 0);
	notification_fd = ck_completion_queue_notify_fd(&queue);
	assert(notification_fd >= 0);

	epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	assert(epoll_fd >= 0);
	memset(&event, 0, sizeof(event));
	event.events = EPOLLIN;
	event.data.fd = notification_fd;
	assert(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, notification_fd, &event) == 0);

	left.queue = &queue;
	left.base = 100;
	right.queue = &queue;
	right.base = 200;
	assert(pthread_create(&left_thread, NULL, producer_main, &left) == 0);
	assert(pthread_create(&right_thread, NULL, producer_main, &right) == 0);
	assert(pthread_join(left_thread, NULL) == 0);
	assert(pthread_join(right_thread, NULL) == 0);
	assert(left.result == 0);
	assert(right.result == 0);

	result = epoll_wait(epoll_fd, &event, 1, 1000);
	assert(result == 1);
	assert(event.data.fd == notification_fd);
	assert(ck_completion_queue_drain_notification(&queue) == 0);

	while ((result = ck_completion_queue_pop(&queue, &record)) == 1)
	{
		assert(record.status == 0);
		assert(record.request_id == 100 + (uint64_t)record.result
				|| record.request_id == 200 + (uint64_t)record.result);
		count++;
	}
	assert(result == 0);
	assert(count == 16);

	{
		unsigned int i;
		for (i = 0; i < CK_COMPLETION_QUEUE_CAPACITY; i++)
		{
			record.request_id = 10000 + i;
			assert(ck_completion_queue_push(&queue, &record) == 0);
		}
		record.request_id = 20000;
		assert(ck_completion_queue_push(&queue, &record) == 1);
		while (ck_completion_queue_pop(&queue, &record) == 1)
		{
		}
		assert(ck_completion_queue_drain_notification(&queue) == 0);
	}

	assert(ck_completion_queue_close(&queue) == 0);
	assert(ck_completion_queue_drain_notification(&queue) == 0);
	assert(ck_completion_queue_close(&queue) == 1);
	assert(ck_completion_queue_push(&queue, &record) == 2);

	close(epoll_fd);
	ck_completion_queue_destroy(&queue);
	return 0;
}
