#ifndef CKARTA_JNI_RUNTIME_H
#define CKARTA_JNI_RUNTIME_H

#include <jni.h>
#include <pthread.h>

#include "ck_request.h"
#include "ck_completion.h"
#include <stddef.h>
#include <stdint.h>

struct ck_runtime
{
	JavaVM *vm;
	pthread_t bootstrap_thread;
	pthread_t worker_thread;
	pthread_mutex_t lock;
	pthread_cond_t condition;
	const char *class_path;
	int bootstrap_done;
	int bootstrap_status;
	int shutdown_requested;
	int shutdown_status;
};

typedef struct ck_runtime ck_runtime_t;

int ck_runtime_init(ck_runtime_t *runtime, const char *class_path);
int ck_runtime_dispatch_async_smoke(ck_runtime_t *runtime, ck_request_t *request,
		ck_completion_t *completion);
int ck_runtime_poll_completion(ck_request_t *runtime_request,
		ck_completion_t *completion);
int ck_runtime_shutdown(ck_runtime_t *runtime);
void ck_runtime_destroy(ck_runtime_t *runtime);

#endif
