#include "ck_async_bridge.h"

#include "../connection/ck_connection_registry.h"

#include <stdint.h>

static ck_connection_registry_t *ck_jni_registry_from_handle(
		jlong registry_handle)
{
	_Static_assert(sizeof(uintptr_t) <= sizeof(jlong),
			"registry pointer must fit in jlong");

	if (registry_handle <= 0)
	{
		return NULL;
	}

	return (ck_connection_registry_t *)(uintptr_t)(uint64_t)registry_handle;
}

static jint ck_native_start_async_cycle(
		JNIEnv *env, jclass clazz,
		jlong registry_handle, jlong connection_handle,
		jlong request_id, jlong owner_token,
		jlong lifetime_token, jlong cycle_id)
{
	ck_connection_registry_t *registry;

	(void)env;
	(void)clazz;

	registry = ck_jni_registry_from_handle(registry_handle);
	if (registry == NULL || connection_handle <= 0
			|| request_id <= 0 || owner_token < 0
			|| lifetime_token < 0 || cycle_id <= 0)
	{
		return -1;
	}

	return (jint)ck_connection_registry_start_async_cycle(
			registry,
			(uint64_t)connection_handle,
			(uint64_t)request_id,
			(uint64_t)owner_token,
			(uint64_t)lifetime_token,
			(uint64_t)cycle_id);
}

static jint ck_native_try_terminal(
		JNIEnv *env, jclass clazz,
		jlong registry_handle, jlong connection_handle,
		jlong request_id, jlong owner_token,
		jlong lifetime_token, jlong cycle_id, jint event)
{
	ck_connection_registry_t *registry;

	(void)env;
	(void)clazz;

	registry = ck_jni_registry_from_handle(registry_handle);
	if (registry == NULL || connection_handle <= 0
			|| request_id <= 0 || owner_token < 0
			|| lifetime_token < 0 || cycle_id <= 0
			|| event < CK_CONNECTION_TERMINAL_COMPLETE
			|| event > CK_CONNECTION_TERMINAL_SHUTDOWN)
	{
		return -1;
	}

	return (jint)ck_connection_registry_try_terminal(
			registry,
			(uint64_t)connection_handle,
			(uint64_t)request_id,
			(uint64_t)owner_token,
			(uint64_t)lifetime_token,
			(uint64_t)cycle_id,
			(ck_connection_terminal_event_t)event);
}

int ck_jni_async_bridge_register(JNIEnv *env)
{
	jclass bridge_class;
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
	static const JNINativeMethod methods[] = {
		{ "nativeStartAsyncCycle", "(JJJJJJ)I",
				(void *)ck_native_start_async_cycle },
		{ "nativeTryTerminal", "(JJJJJJI)I",
				(void *)ck_native_try_terminal }
	};
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

	if (env == NULL)
	{
		return -1;
	}

	bridge_class = (*env)->FindClass(
			env, "org/ckarta/servlet/CkartaNativeAsyncBridge");
	if (bridge_class == NULL)
	{
		return -1;
	}

	if ((*env)->RegisterNatives(
			env, bridge_class, methods,
			(jint)(sizeof(methods) / sizeof(methods[0])))
			!= JNI_OK)
	{
		(*env)->DeleteLocalRef(env, bridge_class);
		return -1;
	}

	(*env)->DeleteLocalRef(env, bridge_class);
	return 0;
}
