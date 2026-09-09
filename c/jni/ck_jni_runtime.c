#include "ck_jni_runtime.h"

#include "ck_request.h"
#include "../error/ck_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int ck_call_start(JNIEnv *env)
{
	jclass runtime_class;
	jmethodID method;

	runtime_class = (*env)->FindClass(env, "org/ckarta/bootstrap/CkartaRuntime");
	if (runtime_class == NULL || ck_check_java_exception(env, "FindClass") != 0)
	{
		return -1;
	}

	method = (*env)->GetStaticMethodID(env, runtime_class, "start", "()V");
	if (method == NULL || ck_check_java_exception(env, "GetStaticMethodID(start)") != 0)
	{
		(*env)->DeleteLocalRef(env, runtime_class);
		return -1;
	}

	(*env)->CallStaticVoidMethod(env, runtime_class, method);
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
	if (runtime_class == NULL || ck_check_java_exception(env, "FindClass(stop)") != 0)
	{
		return -1;
	}

	method = (*env)->GetStaticMethodID(env, runtime_class, "stop", "()V");
	if (method == NULL || ck_check_java_exception(env, "GetStaticMethodID(stop)") != 0)
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


	option_length = snprintf(option_string, sizeof(option_string),
			"-Djava.class.path=%s", runtime->class_path);
	if (option_length < 0 || (size_t)option_length >= sizeof(option_string))
	{
		pthread_mutex_lock(&runtime->lock);
		runtime->bootstrap_status = JNI_EINVAL;
		runtime->bootstrap_done = 1;
		pthread_cond_broadcast(&runtime->condition);
		pthread_mutex_unlock(&runtime->lock);
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

	pthread_mutex_lock(&runtime->lock);
	if (create_result != JNI_OK)
	{
		runtime->bootstrap_status = create_result;
		runtime->bootstrap_done = 1;
		pthread_cond_broadcast(&runtime->condition);
		pthread_mutex_unlock(&runtime->lock);
		return NULL;
	}

	runtime->vm = vm;
	pthread_mutex_unlock(&runtime->lock);

	start_status = ck_call_start(env);

	pthread_mutex_lock(&runtime->lock);
	runtime->bootstrap_status = start_status;
	runtime->bootstrap_done = 1;
	pthread_cond_broadcast(&runtime->condition);

	while (!runtime->shutdown_requested && start_status == 0)
	{
		pthread_cond_wait(&runtime->condition, &runtime->lock);
	}

	pthread_mutex_unlock(&runtime->lock);

	if (start_status == 0)
	{
		start_status = ck_call_stop(env);
		runtime->shutdown_status = start_status == 0 ? 0 : -1;
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
	jobject buffer;

	runtime_class = (*env)->FindClass(env, "org/ckarta/bootstrap/CkartaRuntime");
	if (runtime_class == NULL ||
			ck_check_java_exception(env, "FindClass(dispatchAsync)") != 0)
	{
		return -1;
	}

	method = (*env)->GetStaticMethodID(env, runtime_class, "dispatchAsync",
			"(JJJLjava/nio/ByteBuffer;)V");
	if (method == NULL ||
			ck_check_java_exception(env, "GetStaticMethodID(dispatchAsync)") != 0)
	{
		(*env)->DeleteLocalRef(env, runtime_class);
		return -1;
	}

	buffer = (*env)->NewDirectByteBuffer(env, (void *)descriptor->body,
			(jlong)descriptor->body_length);
	if (buffer == NULL ||
			ck_check_java_exception(env, "NewDirectByteBufferAsync") != 0)
	{
		(*env)->DeleteLocalRef(env, runtime_class);
		return -1;
	}

	(*env)->CallStaticVoidMethod(env, runtime_class, method,
			(jlong)descriptor->request_id,
			(jlong)descriptor->owner_token,
			(jlong)descriptor->lifetime_token,
			buffer);
	if (ck_check_java_exception(env, "CallStaticVoidMethod(dispatchAsync)") != 0)
	{
		(*env)->DeleteLocalRef(env, buffer);
		(*env)->DeleteLocalRef(env, runtime_class);
		return -1;
	}

	(*env)->DeleteLocalRef(env, buffer);
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
	JNIEnv *env = NULL;
	jclass runtime_class;
	jmethodID method;
	jobject output;
	unsigned char storage[36];
	void *native_output;
	jint poll_result;
	jlong request_handle;
	jlong owner_token;
	jlong lifetime_token;
	jlong result_value;
	jint status;
	jint attach_result;
	jint detach_result;
	int finish_result;

	if (runtime == NULL || requests == NULL || request_count == 0
			|| runtime->vm == NULL || runtime->shutdown_requested
			|| runtime->shutdown_complete)
	{
		return -1;
	}

	attach_result = (*runtime->vm)->AttachCurrentThread(runtime->vm,
			(void **)&env, NULL);
	if (attach_result != JNI_OK)
	{
		return -1;
	}

	runtime_class = (*env)->FindClass(env, "org/ckarta/bootstrap/CkartaRuntime");
	if (runtime_class == NULL ||
			ck_check_java_exception(env, "FindClass(pollCompletion)") != 0)
	{
		(void)(*runtime->vm)->DetachCurrentThread(runtime->vm);
		return -1;
	}

	method = (*env)->GetStaticMethodID(env, runtime_class, "pollCompletion",
			"(Ljava/nio/ByteBuffer;)I");
	if (method == NULL ||
			ck_check_java_exception(env, "GetStaticMethodID(pollCompletion)") != 0)
	{
		(*env)->DeleteLocalRef(env, runtime_class);
		(void)(*runtime->vm)->DetachCurrentThread(runtime->vm);
		return -1;
	}

	memset(storage, 0, sizeof(storage));
	native_output = storage;
	output = (*env)->NewDirectByteBuffer(env, native_output, sizeof(storage));
	if (output == NULL ||
			ck_check_java_exception(env, "NewDirectByteBuffer(pollCompletion)") != 0)
	{
		(*env)->DeleteLocalRef(env, runtime_class);
		(void)(*runtime->vm)->DetachCurrentThread(runtime->vm);
		return -1;
	}

	poll_result = (*env)->CallStaticIntMethod(env, runtime_class, method, output);
	if (ck_check_java_exception(env, "CallStaticIntMethod(pollCompletion)") != 0)
	{
		(*env)->DeleteLocalRef(env, output);
		(*env)->DeleteLocalRef(env, runtime_class);
		(void)(*runtime->vm)->DetachCurrentThread(runtime->vm);
		return -1;
	}

	(*env)->DeleteLocalRef(env, output);
	(*env)->DeleteLocalRef(env, runtime_class);

	detach_result = (*runtime->vm)->DetachCurrentThread(runtime->vm);
	if (detach_result != JNI_OK)
	{
		return -1;
	}

	if (poll_result != 1)
	{
		return poll_result;
	}

	memcpy(&request_handle, storage, sizeof(request_handle));
	memcpy(&owner_token, storage + 8, sizeof(owner_token));
	memcpy(&lifetime_token, storage + 16, sizeof(lifetime_token));
	memcpy(&result_value, storage + 24, sizeof(result_value));
	memcpy(&status, storage + 32, sizeof(status));

	{
		size_t i;

		for (i = 0; i < request_count; i++)
		{
			if (request_handle == (jlong)requests[i].descriptor.request_id
					&& owner_token == (jlong)requests[i].descriptor.owner_token
					&& lifetime_token == (jlong)requests[i].descriptor.lifetime_token)
			{
				printf("CKARTA_DISPATCH handle=%lld owner=%lld lifetime=%lld result=%lld status=%d\\n",
						(long long)request_handle, (long long)owner_token,
						(long long)lifetime_token, (long long)result_value,
						(int)status);

				if (status == 0)
				{
					finish_result = ck_request_finish(&requests[i],
							CK_REQUEST_COMPLETED);
					return finish_result == 0 ? 1 : -2;
				}

				{
					ck_error_t error;
					ck_error_category_t category;
					ck_error_code_t code;
					int32_t http_status;

					if (status == -2)
					{
						category = CK_ERROR_CATEGORY_RESOURCE;
						code = CK_ERROR_CODE_RESOURCE_EXHAUSTED;
						http_status = 503;
					}
					else if (status == -1)
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

					finish_result = ck_request_fail(&requests[i], &error);
					return finish_result == 0 ? -2 : -2;
				}
			}
		}
	}

	return -3;
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
	result = pthread_mutex_init(&runtime->lock, NULL);
	if (result != 0)
	{
		return result;
	}

	result = pthread_cond_init(&runtime->condition, NULL);
	if (result != 0)
	{
		pthread_mutex_destroy(&runtime->lock);
		return result;
	}

	runtime->sync_initialized = 1;

	result = pthread_create(&runtime->bootstrap_thread, NULL, ck_bootstrap_main, runtime);
	if (result != 0)
	{
		pthread_cond_destroy(&runtime->condition);
		pthread_mutex_destroy(&runtime->lock);
		runtime->sync_initialized = 0;
		return result;
	}

	runtime->bootstrap_thread_started = 1;

	result = pthread_mutex_lock(&runtime->lock);
	if (result != 0)
	{
		return result;
	}

	while (!runtime->bootstrap_done)
	{
		result = pthread_cond_wait(&runtime->condition, &runtime->lock);
		if (result != 0)
		{
			(void)pthread_mutex_unlock(&runtime->lock);
			return result;
		}
	}
	result = runtime->bootstrap_status;
	pthread_mutex_unlock(&runtime->lock);

	if (result != 0)
	{
		if (pthread_join(runtime->bootstrap_thread, NULL) != 0)
		{
			return -1;
		}

		pthread_cond_destroy(&runtime->condition);
		pthread_mutex_destroy(&runtime->lock);
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
	if (result != 0)
	{
		return;
	}

	result = pthread_mutex_destroy(&runtime->lock);
	if (result != 0)
	{
		return;
	}

	memset(runtime, 0, sizeof(*runtime));
}
