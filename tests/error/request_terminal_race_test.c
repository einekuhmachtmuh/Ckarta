#include "../../c/jni/ck_request.h"

#include <assert.h>
#include <pthread.h>
#include <string.h>

typedef struct { ck_request_t *request; int result; } race_args_t;

static void *cancel_thread(void *arg)
{
	race_args_t *args = arg;
	args->result = ck_request_cancel(args->request);
	return NULL;
}

static void *finish_thread(void *arg)
{
	race_args_t *args = arg;
	args->result = ck_request_finish(args->request);
	return NULL;
}

static int init_request(ck_request_t *request, ck_request_descriptor_t *d,
		unsigned char *body, uint64_t request_id)
{
	memset(d, 0, sizeof(*d));
	d->abi_version = CK_JNI_ABI_VERSION;
	d->struct_size = sizeof(*d);
	d->feature_flags = CK_REQUEST_FEATURE_DIRECT_BUFFER;
	d->ownership_flags = CK_REQUEST_OWNS_NATIVE_STORAGE |
			CK_REQUEST_JAVA_BORROWS_BUFFER;
	d->owner_token = request_id + 1;
	d->lifetime_token = request_id + 2;
	d->request_id = request_id;
	d->body = body;
	d->body_length = 1;
	return ck_request_init(request, d);
}

int main(void)
{
	unsigned char body[] = "x";
	ck_request_descriptor_t descriptor;
	ck_request_t request;
	race_args_t cancel = { 0 };
	race_args_t finish = { 0 };
	pthread_t cancel_tid;
	pthread_t finish_tid;
	int winners;

	assert(init_request(&request, &descriptor, body, 101) == 0);
	assert(ck_request_begin(&request) == 0);
	cancel.request = &request;
	finish.request = &request;
	assert(pthread_create(&cancel_tid, NULL, cancel_thread, &cancel) == 0);
	assert(pthread_create(&finish_tid, NULL, finish_thread, &finish) == 0);
	assert(pthread_join(cancel_tid, NULL) == 0);
	assert(pthread_join(finish_tid, NULL) == 0);
	winners = (cancel.result == 0) + (finish.result == 0);
	assert(winners == 1);
	assert(ck_request_state(&request) == CK_REQUEST_CANCELLING
			|| ck_request_state(&request) == CK_REQUEST_COMPLETED);
	if (ck_request_state(&request) == CK_REQUEST_CANCELLING)
	{
		assert(ck_request_finish(&request) == 1);
	}

	assert(init_request(&request, &descriptor, body, 202) == 0);
	assert(ck_request_begin(&request) == 0);
	cancel.request = &request;
	finish.request = &request;
	assert(ck_request_finish(&request) == 0);
	assert(ck_request_cancel(&request) == 1);
	assert(ck_request_state(&request) == CK_REQUEST_COMPLETED);

	return 0;
}
