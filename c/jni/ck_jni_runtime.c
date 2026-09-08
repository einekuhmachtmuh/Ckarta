#include "ck_jni_runtime.h"

#include "ck_request.h"

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
	start_status = ck_call_start(env);
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
		return NULL;
	}

	worker->result = 0;
	for (i = 0; i < worker->request_count; i++)
	{
		if (ck_request_begin(&worker->requests[i]) != 0 ||
				ck_call_dispatch_async(env, &worker->requests[i].descriptor) != 0)
		{
			if (ck_request_state(&worker->requests[i]) == CK_REQUEST_RUNNING)
			{
				(void)ck_request_finish(&worker->requests[i],
						CK_REQUEST_FAILED);
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

	if (runtime == NULL || requests == NULL || request_count == 0)
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

				finish_result = ck_request_finish(&requests[i],
						status == 0 ? CK_REQUEST_COMPLETED : CK_REQUEST_FAILED);
				return finish_result == 0 && status == 0 ? 1 : -2;
			}
		}
	}

	return -3;
}

int ck_runtime_init(ck_runtime_t *runtime, const char *class_path)
{
	int result;

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

	result = pthread_create(&runtime->bootstrap_thread, NULL, ck_bootstrap_main, runtime);
	if (result != 0)
	{
		pthread_cond_destroy(&runtime->condition);
		pthread_mutex_destroy(&runtime->lock);
		return result;
	}

	pthread_mutex_lock(&runtime->lock);
	while (!runtime->bootstrap_done)
	{
		pthread_cond_wait(&runtime->condition, &runtime->lock);
	}
	result = runtime->bootstrap_status;
	pthread_mutex_unlock(&runtime->lock);

	if (result != 0)
	{
		pthread_join(runtime->bootstrap_thread, NULL);
		return -1;
	}

	return 0;
}

int ck_runtime_shutdown(ck_runtime_t *runtime)
{
	pthread_mutex_lock(&runtime->lock);
	runtime->shutdown_requested = 1;
	pthread_cond_broadcast(&runtime->condition);
	pthread_mutex_unlock(&runtime->lock);

	if (pthread_join(runtime->bootstrap_thread, NULL) != 0)
	{
		return -1;
	}

	return runtime->shutdown_status;
}

void ck_runtime_destroy(ck_runtime_t *runtime)
{
	pthread_cond_destroy(&runtime->condition);
	pthread_mutex_destroy(&runtime->lock);
}
