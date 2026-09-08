#ifndef CKARTA_JNI_RUNTIME_H
#define CKARTA_JNI_RUNTIME_H

#include <jni.h>
#include <pthread.h>
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
int ck_runtime_dispatch_smoke(ck_runtime_t *runtime, uintptr_t request_handle,
		const unsigned char *body, size_t body_length);
int ck_runtime_shutdown(ck_runtime_t *runtime);
void ck_runtime_destroy(ck_runtime_t *runtime);

#endif
