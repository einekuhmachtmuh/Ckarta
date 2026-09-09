#include "../c/connection/ck_connection_registry.h"
#include "../c/jni/ck_async_bridge.h"

#include <assert.h>
#include <jni.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CKARTA_TEST_CLASSPATH "build/classes:build/java-test-classes:build/deps/jakarta.servlet-api-6.1.0.jar"

static int check_java_exception(JNIEnv *env, const char *operation)
{
	if (!(*env)->ExceptionCheck(env))
	{
		return 0;
	}

	fprintf(stderr, "CKARTA_NATIVE_BRIDGE_JAVA_EXCEPTION=%s\n", operation);
	(*env)->ExceptionDescribe(env);
	(*env)->ExceptionClear(env);
	return -1;
}

int main(void)
{
	JavaVM *vm = NULL;
	JNIEnv *env = NULL;
	JavaVMOption option;
	JavaVMInitArgs args;
	char option_string[1024];
	jint create_result;
	jclass test_class;
	jmethodID method;
	ck_connection_registry_t registry;
	ck_connection_handle_t first_handle;
	ck_connection_handle_t second_handle;
	int first_socket_pair[2];
	int second_socket_pair[2];
	char byte;
	int result;

	_Static_assert(sizeof(uintptr_t) <= sizeof(jlong),
			"registry pointer must fit in jlong");

	memset(&registry, 0, sizeof(registry));
	assert(ck_connection_registry_init(&registry) == 0);
	assert(ck_connection_registry_register(
			&registry, 1, 11, 22, 33, &first_handle) == 0);
	assert(ck_connection_registry_register(
			&registry, 2, 44, 55, 66, &second_handle) == 0);
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, first_socket_pair) == 0);
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, second_socket_pair) == 0);
	assert(ck_connection_registry_attach_socket(
			&registry, first_handle, 11, 22, 33, first_socket_pair[0]) == 0);
	assert(ck_connection_registry_attach_socket(
			&registry, second_handle, 44, 55, 66, second_socket_pair[0]) == 0);
	assert(ck_connection_registry_socket_fd(
			&registry, first_handle, 11, 22, 33) == first_socket_pair[0]);
	assert(ck_connection_registry_socket_fd(
			&registry, second_handle, 44, 55, 66) == second_socket_pair[0]);

	result = snprintf(option_string, sizeof(option_string),
			"-Djava.class.path=%s", CKARTA_TEST_CLASSPATH);
	assert(result > 0 && (size_t)result < sizeof(option_string));

	option.optionString = option_string;
	option.extraInfo = NULL;
	memset(&args, 0, sizeof(args));
	args.version = JNI_VERSION_21;
	args.nOptions = 1;
	args.options = &option;
	args.ignoreUnrecognized = JNI_FALSE;

	create_result = JNI_CreateJavaVM(&vm, (void **)&env, &args);
	assert(create_result == JNI_OK);
	assert(env != NULL);
	assert(vm != NULL);

	assert(ck_jni_async_bridge_register(env) == 0);
	assert(check_java_exception(env, "ck_jni_async_bridge_register") == 0);

	test_class = (*env)->FindClass(
			env, "org/ckarta/servlet/CkartaNativeAsyncBridgeTest");
	assert(check_java_exception(env, "FindClass") == 0);
	assert(test_class != NULL);

	method = (*env)->GetStaticMethodID(
			env, test_class, "run", "(JJJ)V");
	assert(check_java_exception(env, "GetStaticMethodID(run)") == 0);
	assert(method != NULL);

	(*env)->CallStaticVoidMethod(
			env, test_class, method,
			(jlong)(uintptr_t)(void *)&registry,
			(jlong)first_handle,
			(jlong)second_handle);
	assert(check_java_exception(env, "CallStaticVoidMethod(run)") == 0);

	(*env)->DeleteLocalRef(env, test_class);

	assert(ck_connection_registry_close(
			&registry, first_handle, 11, 22, 33) == 0);
	assert(ck_connection_registry_socket_fd(
			&registry, first_handle, 11, 22, 33) == -1);
	assert(recv(first_socket_pair[1], &byte, 1, 0) == 0);
	assert(close(first_socket_pair[1]) == 0);
	assert(ck_connection_registry_retire(
			&registry, first_handle, 11, 22, 33) == 0);

	assert(ck_connection_registry_close(
			&registry, second_handle, 44, 55, 66) == 0);
	assert(ck_connection_registry_socket_fd(
			&registry, second_handle, 44, 55, 66) == -1);
	assert(recv(second_socket_pair[1], &byte, 1, 0) == 0);
	assert(close(second_socket_pair[1]) == 0);
	assert(ck_connection_registry_retire(
			&registry, second_handle, 44, 55, 66) == 0);

	assert(ck_connection_registry_active_count(&registry) == 0);
	assert(ck_connection_registry_destroy(&registry) == 0);

	assert((*vm)->DestroyJavaVM(vm) == JNI_OK);
	printf("CKARTA_NATIVE_ASYNC_BRIDGE_OK\n");
	return 0;
}
