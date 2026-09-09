#ifndef CKARTA_COMPLETION_NOTIFICATION_H
#define CKARTA_COMPLETION_NOTIFICATION_H

typedef struct ck_completion_notification
{
	int fd;
	int initialized;
} ck_completion_notification_t;

int ck_completion_notification_init(ck_completion_notification_t *notification);
int ck_completion_notification_signal(ck_completion_notification_t *notification);
int ck_completion_notification_fd(const ck_completion_notification_t *notification);
int ck_completion_notification_drain(ck_completion_notification_t *notification);
void ck_completion_notification_destroy(ck_completion_notification_t *notification);

#endif
