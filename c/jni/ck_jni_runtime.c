#include "ck_jni_runtime.h"

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

static int ck_call_dispatch(JNIEnv *env, uintptr_t request_handle,
		const unsigned char *body, size_t body_length)
{
	jclass runtime_class;
	jmethodID method;
	jobject buffer;
	jlong value;
	jlong expected;

	runtime_class = (*env)->FindClass(env, "org/ckarta/bootstrap/CkartaRuntime");
	if (runtime_class == NULL || ck_check_java_exception(env, "FindClass(dispatch)") != 0)
	{
		return -1;
	}

	method = (*env)->GetStaticMethodID(env, runtime_class, "dispatch",
			"(JLjava/nio/ByteBuffer;)J");
	if (method == NULL || ck_check_java_exception(env, "GetStaticMethodID(dispatch)") != 0)
	{
		(*env)->DeleteLocalRef(env, runtime_class);
		return -1;
	}

	buffer = (*env)->NewDirectByteBuffer(env, (void *)body, (jlong)body_length);
	if (buffer == NULL || ck_check_java_exception(env, "NewDirectByteBuffer") != 0)
	{
		(*env)->DeleteLocalRef(env, runtime_class);
		return -1;
	}

	value = (*env)->CallStaticLongMethod(env, runtime_class, method,
			(jlong)request_handle, buffer);
	if (ck_check_java_exception(env, "CallStaticLongMethod(dispatch)") != 0)
	{
		(*env)->DeleteLocalRef(env, buffer);
		(*env)->DeleteLocalRef(env, runtime_class);
		return -1;
	}

	expected = (jlong)request_handle + (jlong)body_length;
	printf("CKARTA_DISPATCH handle=%llu length=%zu result=%lld expected=%lld\n",
			(unsigned long long)request_handle, body_length,
			(long long)value, (long long)expected);

	(*env)->DeleteLocalRef(env, buffer);
	(*env)->DeleteLocalRef(env, runtime_class);
	return value == expected ? 0 : -1;
}

struct ck_worker_args
{
	ck_runtime_t *runtime;
	uintptr_t request_handle;
	const unsigned char *body;
	size_t body_length;
	int result;
};

static void *ck_worker_main(void *arg)
{
	struct ck_worker_args *worker = arg;
	JNIEnv *env = NULL;
	jint result;

	result = (*worker->runtime->vm)->AttachCurrentThread(worker->runtime->vm,
			(void **)&env, NULL);
	if (result != JNI_OK)
	{
		worker->result = result;
		return NULL;
	}

	worker->result = ck_call_dispatch(env, worker->request_handle,
			worker->body, worker->body_length);
	result = (*worker->runtime->vm)->DetachCurrentThread(worker->runtime->vm);
	if (result != JNI_OK && worker->result == 0)
	{
		worker->result = result;
	}

	return NULL;
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

int ck_runtime_dispatch_smoke(ck_runtime_t *runtime, uintptr_t request_handle,
		const unsigned char *body, size_t body_length)
{
	struct ck_worker_args worker;
	int result;

	memset(&worker, 0, sizeof(worker));
	worker.runtime = runtime;
	worker.request_handle = request_handle;
	worker.body = body;
	worker.body_length = body_length;

	result = pthread_create(&runtime->worker_thread, NULL, ck_worker_main, &worker);
	if (result != 0)
	{
		return result;
	}
	result = pthread_join(runtime->worker_thread, NULL);
	if (result != 0)
	{
		return result;
	}

	return worker.result;
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
