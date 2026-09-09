#include "ck_jni_runtime.h"

#include "ck_request.h"
#include "../error/ck_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static void ck_runtime_sync_fatal(const char *operation, int result)
{
	if (result != 0)
	{
		fprintf(stderr, "CKARTA_SYNC_ERROR=%s:%d\n", operation, result);
		abort();
	}
}

static int ck_check_java_exception(JNIEnv *env, const char *operation)
{
	if (!(*env)->ExceptionCheck(env))
	{
		return 0;
	}

	fprintf(stderr, "CKARTA_JAVA_EXCEPTION=%s\n", operation);
	(*env)->ExceptionDescribe(env);
	(*env)->ExceptionClear(env);
	return -1;
}


static jint ck_native_publish_completion(JNIEnv *env, jclass clazz,
		jlong queue_handle, jlong request_id, jlong owner_token,
		jlong lifetime_token, jlong cycle_id, jlong result, jint status)
{
	ck_completion_record_t record;
	ck_completion_queue_t *queue;

	(void)env;
	(void)clazz;

	if (queue_handle <= 0 || request_id <= 0
			|| owner_token < 0 || lifetime_token < 0 || cycle_id <= 0)
	{
		return -1;
	}

	queue = (ck_completion_queue_t *)(uintptr_t)(uint64_t)queue_handle;
	if (queue == NULL)
	{
		return -1;
	}

	record.request_id = (uint64_t)request_id;
	record.owner_token = (uint64_t)owner_token;
	record.lifetime_token = (uint64_t)lifetime_token;
	record.cycle_id = (uint64_t)cycle_id;
	record.result = (int64_t)result;
	record.status = (int32_t)status;

	return (jint)ck_completion_queue_push_wait(queue, &record);
}

static int ck_call_start(JNIEnv *env, ck_completion_queue_t *queue)
{
	jclass runtime_class;
	jmethodID method;
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
	static const JNINativeMethod methods[] = {
		{ "publishCompletion", "(JJJJJJI)I",
				(void *)ck_native_publish_completion }
	};
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
	jlong queue_handle;

	if (env == NULL || queue == NULL)
	{
		return -1;
	}

	_Static_assert(sizeof(uintptr_t) <= sizeof(jlong),
			"runtime queue pointer must fit in jlong");

	queue_handle = (jlong)(uintptr_t)(void *)queue;
	if (queue_handle <= 0)
	{
		return -1;
	}

	runtime_class = (*env)->FindClass(env, "org/ckarta/bootstrap/CkartaRuntime");
	if (ck_check_java_exception(env, "FindClass") != 0 || runtime_class == NULL)
	{
		return -1;
	}

	if ((*env)->RegisterNatives(env, runtime_class, methods,
			(jint)(sizeof(methods) / sizeof(methods[0]))) != JNI_OK)
	{
		ck_check_java_exception(env, "RegisterNatives");
		(*env)->DeleteLocalRef(env, runtime_class);
		return -1;
	}

	method = (*env)->GetStaticMethodID(env, runtime_class, "start", "(J)V");
	if (ck_check_java_exception(env, "GetStaticMethodID(start)") != 0 || method == NULL)
	{
		(*env)->DeleteLocalRef(env, runtime_class);
		return -1;
	}

	(*env)->CallStaticVoidMethod(env, runtime_class, method, queue_handle);
	if (ck_check_java_exception(env, "CallStaticVoidMethod(start)") != 0)
	{
		(*env)->DeleteLocalRef(env, runtime_class);
		return -1;
	}

	(*env)->DeleteLocalRef(env, runtime_class);
	return 0;
}
static int ck_call_stop(JNIEnv *env)
{
	jclass runtime_class;
	jmethodID method;

	runtime_class = (*env)->FindClass(env, "org/ckarta/bootstrap/CkartaRuntime");
	if (ck_check_java_exception(env, "FindClass(stop)") != 0 || runtime_class == NULL)
	{
		return -1;
	}

	method = (*env)->GetStaticMethodID(env, runtime_class, "stop", "()V");
	if (ck_check_java_exception(env, "GetStaticMethodID(stop)") != 0 || method == NULL)
	{
		(*env)->DeleteLocalRef(env, runtime_class);
		return -1;
	}

	(*env)->CallStaticVoidMethod(env, runtime_class, method);
	if (ck_check_java_exception(env, "CallStaticVoidMethod(stop)") != 0)
	{
		(*env)->DeleteLocalRef(env, runtime_class);
		return -1;
	}

	(*env)->DeleteLocalRef(env, runtime_class);
	return 0;
}

static void *ck_bootstrap_main(void *arg)
{
	ck_runtime_t *runtime = arg;
	JNIEnv *env = NULL;
	JavaVMOption option;
	JavaVMInitArgs args;
	JavaVM *vm = NULL;
	char option_string[4096];
	jint create_result;
	int start_status;
	int option_length;
	int result;
	int stop_status;


	option_length = snprintf(option_string, sizeof(option_string),
			"-Djava.class.path=%s", runtime->class_path);
	if (option_length < 0 || (size_t)option_length >= sizeof(option_string))
	{
		ck_runtime_sync_fatal("bootstrap.mutex_lock", pthread_mutex_lock(&runtime->lock));
		runtime->bootstrap_status = JNI_EINVAL;
		runtime->bootstrap_done = 1;
		ck_runtime_sync_fatal("bootstrap.cond_broadcast", pthread_cond_broadcast(&runtime->condition));
		ck_runtime_sync_fatal("bootstrap.mutex_unlock", pthread_mutex_unlock(&runtime->lock));
		return NULL;
	}

	option.optionString = option_string;
	option.extraInfo = NULL;
	memset(&args, 0, sizeof(args));
	args.version = JNI_VERSION_21;
	args.nOptions = 1;
	args.options = &option;
	args.ignoreUnrecognized = JNI_TRUE;

	create_result = JNI_CreateJavaVM(&vm, (void **)&env, &args);

	ck_runtime_sync_fatal("bootstrap.mutex_lock", pthread_mutex_lock(&runtime->lock));
	if (create_result != JNI_OK)
	{
		runtime->bootstrap_status = create_result;
		runtime->bootstrap_done = 1;
		ck_runtime_sync_fatal("bootstrap.cond_broadcast", pthread_cond_broadcast(&runtime->condition));
		ck_runtime_sync_fatal("bootstrap.mutex_unlock", pthread_mutex_unlock(&runtime->lock));
		return NULL;
	}

	runtime->vm = vm;
	ck_runtime_sync_fatal("bootstrap.mutex_unlock", pthread_mutex_unlock(&runtime->lock));

	start_status = ck_call_start(env, &runtime->completion_queue);

	ck_runtime_sync_fatal("bootstrap.mutex_lock", pthread_mutex_lock(&runtime->lock));
	runtime->bootstrap_status = start_status;
	runtime->bootstrap_done = 1;
	ck_runtime_sync_fatal("bootstrap.cond_broadcast", pthread_cond_broadcast(&runtime->condition));

	while (!runtime->shutdown_requested && start_status == 0)
	{
		ck_runtime_sync_fatal("bootstrap.cond_wait", pthread_cond_wait(&runtime->condition, &runtime->lock));
	}

	ck_runtime_sync_fatal("bootstrap.mutex_unlock", pthread_mutex_unlock(&runtime->lock));

	if (start_status == 0)
	{
		result = ck_completion_queue_close(&runtime->completion_queue);
		if (result != 0 && result != 1)
		{
			runtime->shutdown_status = result;
			start_status = result;
		}

		stop_status = ck_call_stop(env);
		if (stop_status != 0 && runtime->shutdown_status == 0)
		{
			runtime->shutdown_status = -1;
		}
	}
	else
	{
		runtime->shutdown_status = start_status;
	}

	create_result = (*vm)->DestroyJavaVM(vm);
	if (create_result != JNI_OK && runtime->shutdown_status == 0)
	{
		runtime->shutdown_status = create_result;
	}

	return NULL;
}

struct ck_worker_args
{
	ck_runtime_t *runtime;
	ck_request_t *requests;
	size_t request_count;
	int result;
};

static int ck_call_dispatch_async(JNIEnv *env,
		const ck_request_descriptor_t *descriptor)
{
	jclass runtime_class;
	jmethodID method;
	jobject metadata_buffer;
	jobject body_buffer;

	runtime_class = (*env)->FindClass(env, "org/ckarta/bootstrap/CkartaRuntime");
	if (ck_check_java_exception(env, "FindClass(dispatchAsync)") != 0 ||
			runtime_class == NULL)
	{
		return -1;
	}

	method = (*env)->GetStaticMethodID(env, runtime_class, "dispatchAsync",
			"(JJJLjava/nio/ByteBuffer;)V");
	if (ck_check_java_exception(env, "GetStaticMethodID(dispatchAsync)") != 0 ||
			method == NULL)
	{
		(*env)->DeleteLocalRef(env, runtime_class);
		return -1;
	}

	metadata_buffer = NULL;
	body_buffer = NULL;
	if (descriptor->metadata_length != 0)
	{
		metadata_buffer = (*env)->NewDirectByteBuffer(env,
				(void *)descriptor->metadata,
				(jlong)descriptor->metadata_length);
		if (ck_check_java_exception(env, "NewDirectByteBufferMetadata") != 0
				|| metadata_buffer == NULL)
		{
			(*env)->DeleteLocalRef(env, runtime_class);
			return -1;
		}
	}
	if (descriptor->body_length != 0)
	{
		body_buffer = (*env)->NewDirectByteBuffer(env,
				(void *)descriptor->body,
				(jlong)descriptor->body_length);
		if (ck_check_java_exception(env, "NewDirectByteBufferBody") != 0
				|| body_buffer == NULL)
		{
			if (metadata_buffer != NULL)
			{
				(*env)->DeleteLocalRef(env, metadata_buffer);
			}
			(*env)->DeleteLocalRef(env, runtime_class);
			return -1;
		}
	}

	(*env)->CallStaticVoidMethod(env, runtime_class, method,
			(jlong)descriptor->request_id,
			(jlong)descriptor->owner_token,
			(jlong)descriptor->lifetime_token,
			metadata_buffer,
			body_buffer);
	if (ck_check_java_exception(env, "CallStaticVoidMethod(dispatchAsync)") != 0)
	{
		if (body_buffer != NULL)
		{
			(*env)->DeleteLocalRef(env, body_buffer);
		}
		if (metadata_buffer != NULL)
		{
			(*env)->DeleteLocalRef(env, metadata_buffer);
		}
		(*env)->DeleteLocalRef(env, runtime_class);
		return -1;
	}

	if (body_buffer != NULL)
	{
		(*env)->DeleteLocalRef(env, body_buffer);
	}
	if (metadata_buffer != NULL)
	{
		(*env)->DeleteLocalRef(env, metadata_buffer);
	}
	(*env)->DeleteLocalRef(env, runtime_class);
	return 0;
}

static void *ck_worker_main(void *arg)
{
	struct ck_worker_args *worker = arg;
	JNIEnv *env = NULL;
	jint result;
	size_t i;

	if (worker->request_count == 0)
	{
		worker->result = -1;
		return NULL;
	}

	result = (*worker->runtime->vm)->AttachCurrentThread(worker->runtime->vm,
			(void **)&env, NULL);
	if (result != JNI_OK)
	{
		worker->result = result;
		return NULL;
	}

	worker->result = 0;
	for (i = 0; i < worker->request_count; i++)
	{
		if (ck_request_begin(&worker->requests[i]) != 0)
		{
			worker->result = -1;
			continue;
		}

		if (ck_call_dispatch_async(env,
				&worker->requests[i].descriptor) != 0)
		{
			ck_error_t error;

			ck_error_init(&error);
			if (ck_error_set(&error, CK_ERROR_CATEGORY_JNI,
					CK_ERROR_CODE_JNI_FAILURE, 500,
					CK_ERROR_FLAG_CLIENT_VISIBLE, 1,
					worker->requests[i].descriptor.request_id) != 0
					|| ck_request_fail(&worker->requests[i], &error) != 0)
			{
				worker->result = -1;
				continue;
			}

			worker->result = -1;
		}
	}

	if ((*worker->runtime->vm)->DetachCurrentThread(worker->runtime->vm) != JNI_OK
			&& worker->result == 0)
	{
		worker->result = -1;
	}

	return NULL;
}

int ck_runtime_dispatch_async_smoke(ck_runtime_t *runtime,
		ck_request_t *requests, size_t request_count)
{
	struct ck_worker_args worker;
	int result;

	if (runtime == NULL || requests == NULL || request_count == 0)
	{
		return -1;
	}

	if (runtime->vm == NULL || runtime->shutdown_requested
			|| runtime->shutdown_complete)
	{
		return -1;
	}

	memset(&worker, 0, sizeof(worker));
	worker.runtime = runtime;
	worker.requests = requests;
	worker.request_count = request_count;

	result = pthread_create(&runtime->worker_thread, NULL, ck_worker_main, &worker);
	if (result != 0)
	{
		return result;
	}

	return pthread_join(runtime->worker_thread, NULL) == 0 ? 0 : -1;
}

int ck_runtime_poll_completion(ck_runtime_t *runtime, ck_request_t *requests,
		size_t request_count)
{
	ck_completion_record_t completion;
	int result;
	size_t i;

	if (runtime == NULL || requests == NULL || request_count == 0
			|| !runtime->completion_queue_initialized)
	{
		return -1;
	}

	result = ck_completion_queue_pop(&runtime->completion_queue, &completion);
	if (result <= 0)
	{
		return result;
	}

	for (i = 0; i < request_count; i++)
	{
		if (completion.request_id == requests[i].descriptor.request_id
				&& completion.owner_token == requests[i].descriptor.owner_token
				&& completion.lifetime_token == requests[i].descriptor.lifetime_token
				&& completion.cycle_id == 1U)
		{
			printf("CKARTA_DISPATCH handle=%llu owner=%llu lifetime=%llu result=%lld status=%d\\n",
					(unsigned long long)completion.request_id,
					(unsigned long long)completion.owner_token,
					(unsigned long long)completion.lifetime_token,
					(long long)completion.result,
					(int)completion.status);

			if (completion.status == 0)
			{
				result = ck_request_finish(&requests[i]);
				return result == 0 ? 1 : (result == 1 ? 2 : -1);
			}

			{
				ck_error_t error;
				ck_error_category_t category;
				ck_error_code_t code;
				int32_t http_status;

				if (completion.status == -2)
				{
					category = CK_ERROR_CATEGORY_RESOURCE;
					code = CK_ERROR_CODE_RESOURCE_EXHAUSTED;
					http_status = 503;
				}
				else if (completion.status == -1)
				{
					category = CK_ERROR_CATEGORY_APPLICATION;
					code = CK_ERROR_CODE_APPLICATION_EXCEPTION;
					http_status = 500;
				}
				else
				{
					category = CK_ERROR_CATEGORY_INTERNAL;
					code = CK_ERROR_CODE_INTERNAL_INVARIANT;
					http_status = 500;
				}

				ck_error_init(&error);
				if (ck_error_set(&error, category, code, http_status,
						CK_ERROR_FLAG_CLIENT_VISIBLE, 2,
						requests[i].descriptor.request_id) != 0)
				{
					return -1;
				}

				result = ck_request_fail(&requests[i], &error);
				return result == 0 ? -2 : (result == 1 ? 2 : -1);
			}
		}
	}

	return -3;
}

int ck_runtime_drain_completion_notification(ck_runtime_t *runtime)
{
	if (runtime == NULL || !runtime->completion_queue_initialized)
	{
		return -1;
	}

	return ck_completion_queue_drain_notification(&runtime->completion_queue);
}
int ck_runtime_completion_fd(const ck_runtime_t *runtime)
{
	if (runtime == NULL || !runtime->completion_queue_initialized)
	{
		return -1;
	}

	return ck_completion_queue_notify_fd(&runtime->completion_queue);
}

int ck_runtime_init(ck_runtime_t *runtime, const char *class_path)
{
	int result;

	if (runtime == NULL || class_path == NULL || class_path[0] == '\0')
	{
		return -1;
	}

	memset(runtime, 0, sizeof(*runtime));
	runtime->class_path = class_path;
	result = ck_completion_queue_init(&runtime->completion_queue);
	if (result != 0)
	{
		return result;
	}
	runtime->completion_queue_initialized = 1;

	result = pthread_mutex_init(&runtime->lock, NULL);
	if (result != 0)
	{
		ck_completion_queue_destroy(&runtime->completion_queue);
		runtime->completion_queue_initialized = 0;
		return result;
	}

	result = pthread_cond_init(&runtime->condition, NULL);
	if (result != 0)
	{
		pthread_mutex_destroy(&runtime->lock);
		ck_completion_queue_destroy(&runtime->completion_queue);
		runtime->completion_queue_initialized = 0;
		return result;
	}

	runtime->sync_initialized = 1;

	result = pthread_create(&runtime->bootstrap_thread, NULL, ck_bootstrap_main, runtime);
	if (result != 0)
	{
		pthread_cond_destroy(&runtime->condition);
		pthread_mutex_destroy(&runtime->lock);
		ck_completion_queue_destroy(&runtime->completion_queue);
		runtime->completion_queue_initialized = 0;
		runtime->sync_initialized = 0;
		return result;
	}

	runtime->bootstrap_thread_started = 1;

	result = pthread_mutex_lock(&runtime->lock);
	ck_runtime_sync_fatal("runtime.init.mutex_lock", result);

	while (!runtime->bootstrap_done)
	{
		result = pthread_cond_wait(&runtime->condition, &runtime->lock);
		if (result != 0)
		{
			ck_runtime_sync_fatal("runtime.init.cond_wait", result);
		}
	}
	result = runtime->bootstrap_status;
	ck_runtime_sync_fatal("runtime.init.mutex_unlock", pthread_mutex_unlock(&runtime->lock));

	if (result != 0)
	{
		if (pthread_join(runtime->bootstrap_thread, NULL) != 0)
		{
			return -1;
		}

		pthread_cond_destroy(&runtime->condition);
		pthread_mutex_destroy(&runtime->lock);
		ck_completion_queue_destroy(&runtime->completion_queue);
		runtime->completion_queue_initialized = 0;
		memset(runtime, 0, sizeof(*runtime));
		return -1;
	}

	return 0;
}

int ck_runtime_shutdown(ck_runtime_t *runtime)
{
	int result;
	int shutdown_status;

	if (runtime == NULL || !runtime->sync_initialized
			|| !runtime->bootstrap_thread_started)
	{
		return -1;
	}

	result = pthread_mutex_lock(&runtime->lock);
	if (result != 0)
	{
		return result;
	}

	if (runtime->shutdown_complete)
	{
		shutdown_status = runtime->shutdown_status;
		(void)pthread_mutex_unlock(&runtime->lock);
		return shutdown_status;
	}

	runtime->shutdown_requested = 1;
	result = ck_completion_queue_close(&runtime->completion_queue);
	if (result != 0 && result != 1)
	{
		(void)pthread_mutex_unlock(&runtime->lock);
		return result;
	}

	result = pthread_cond_broadcast(&runtime->condition);
	if (result != 0)
	{
		(void)pthread_mutex_unlock(&runtime->lock);
		return result;
	}

	result = pthread_mutex_unlock(&runtime->lock);
	if (result != 0)
	{
		return result;
	}

	result = pthread_join(runtime->bootstrap_thread, NULL);
	if (result != 0)
	{
		return result;
	}

	result = pthread_mutex_lock(&runtime->lock);
	if (result != 0)
	{
		return result;
	}

	runtime->shutdown_complete = 1;
	shutdown_status = runtime->shutdown_status;
	(void)pthread_mutex_unlock(&runtime->lock);
	return shutdown_status;
}

void ck_runtime_destroy(ck_runtime_t *runtime)
{
	int result;

	if (runtime == NULL || !runtime->sync_initialized
			|| !runtime->shutdown_complete)
	{
		return;
	}

	result = pthread_cond_destroy(&runtime->condition);
	ck_runtime_sync_fatal("runtime.destroy.cond", result);

	result = pthread_mutex_destroy(&runtime->lock);
	ck_runtime_sync_fatal("runtime.destroy.mutex", result);

	if (runtime->completion_queue_initialized)
	{
		ck_completion_queue_destroy(&runtime->completion_queue);
	}

	memset(runtime, 0, sizeof(*runtime));
}
