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

	snprintf(option_string, sizeof(option_string), "-Djava.class.path=%s",
			runtime->class_path);
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
	ck_request_t *request;
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
			"(JLjava/nio/ByteBuffer;)V");
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
			(jlong)descriptor->request_id, buffer);
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

	if (ck_request_begin(worker->request) != 0)
	{
		worker->result = -1;
		return NULL;
	}

	result = (*worker->runtime->vm)->AttachCurrentThread(worker->runtime->vm,
			(void **)&env, NULL);
	if (result != JNI_OK)
	{
		(void)ck_request_finish(worker->request, CK_REQUEST_FAILED);
		worker->result = result;
		return NULL;
	}

	worker->result = ck_call_dispatch_async(env, &worker->request->descriptor);

	if ((*worker->runtime->vm)->DetachCurrentThread(worker->runtime->vm) != JNI_OK
			&& worker->result == 0)
	{
		worker->result = -1;
	}

	return NULL;
}

int ck_runtime_dispatch_async_smoke(ck_runtime_t *runtime,
		ck_request_t *request)
{
	struct ck_worker_args worker;
	int result;

	if (runtime == NULL || request == NULL)
	{
		return -1;
	}

	memset(&worker, 0, sizeof(worker));
	worker.runtime = runtime;
	worker.request = request;

	result = pthread_create(&runtime->worker_thread, NULL, ck_worker_main, &worker);
	if (result != 0)
	{
		return result;
	}

	return pthread_join(runtime->worker_thread, NULL) == 0 ? 0 : -1;
}

int ck_runtime_poll_completion(ck_runtime_t *runtime, ck_request_t *request)
{
	JNIEnv *env = NULL;
	jclass runtime_class;
	jmethodID method;
	jobject output;
	unsigned char storage[20];
	void *native_output;
	jint poll_result;
	jlong request_handle;
	jlong result_value;
	jint status;
	jint attach_result;
	jint detach_result;
	int finish_result;

	if (runtime == NULL || request == NULL)
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
	memcpy(&result_value, storage + 8, sizeof(result_value));
	memcpy(&status, storage + 8 + sizeof(result_value), sizeof(status));

	if (request_handle != (jlong)request->descriptor.request_id)
	{
		return -1;
	}

	finish_result = ck_request_finish(request,
			status == 0 ? CK_REQUEST_COMPLETED : CK_REQUEST_FAILED);
	return finish_result == 0 && status == 0 ? 1 : -1;
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

int ck_runtime_dispatch_async_smoke(ck_runtime_t *runtime,
		ck_request_t *request)
{
	struct ck_worker_args worker;
	int result;

	if (runtime == NULL || request == NULL)
	{
		return -1;
	}

	memset(&worker, 0, sizeof(worker));
	worker.runtime = runtime;
	worker.request = request;

	result = pthread_create(&runtime->worker_thread, NULL, ck_worker_main, &worker);
	if (result != 0)
	{
		return result;
	}

	return pthread_join(runtime->worker_thread, NULL) == 0 ? 0 : -1;
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
