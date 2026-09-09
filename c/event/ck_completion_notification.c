#include "ck_completion_notification.h"

#include <errno.h>
#include <stdint.h>
#include <sys/eventfd.h>
#include <unistd.h>

int ck_completion_notification_init(ck_completion_notification_t *notification)
{
	if (notification == NULL)
	{
		return -1;
	}

	notification->fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (notification->fd < 0)
	{
		return errno;
	}

	notification->initialized = 1;
	return 0;
}

int ck_completion_notification_signal(ck_completion_notification_t *notification)
{
	const uint64_t value = 1;
	ssize_t result;

	if (notification == NULL || !notification->initialized)
	{
		return -1;
	}

	result = write(notification->fd, &value, sizeof(value));
	if (result == (ssize_t)sizeof(value))
	{
		return 0;
	}

	if (result < 0 && errno == EAGAIN)
	{
		return EAGAIN;
	}

	return result < 0 ? errno : EIO;
}

int ck_completion_notification_fd(const ck_completion_notification_t *notification)
{
	if (notification == NULL || !notification->initialized)
	{
		return -1;
	}

	return notification->fd;
}

int ck_completion_notification_drain(ck_completion_notification_t *notification)
{
	uint64_t value;
	ssize_t result;

	if (notification == NULL || !notification->initialized)
	{
		return -1;
	}

	for (;;)
	{
		result = read(notification->fd, &value, sizeof(value));
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

void ck_completion_notification_destroy(ck_completion_notification_t *notification)
{
	if (notification == NULL || !notification->initialized)
	{
		return;
	}

	(void)close(notification->fd);
	notification->fd = -1;
	notification->initialized = 0;
}
