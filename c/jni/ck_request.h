#ifndef CKARTA_REQUEST_H
#define CKARTA_REQUEST_H

#include <stdatomic.h>
#include <stdint.h>

#include "../error/ck_error.h"
#include "../http/ck_http_parser.h"

#define CK_JNI_ABI_VERSION 1u

#define CK_REQUEST_FEATURE_DIRECT_BUFFER (UINT64_C(1) << 0)
#define CK_REQUEST_FEATURE_METADATA_BUFFER (UINT64_C(1) << 1)
#define CK_REQUEST_METADATA_STORAGE_BYTES 8320u

#define CK_REQUEST_OWNS_NATIVE_STORAGE (UINT64_C(1) << 0)
#define CK_REQUEST_JAVA_BORROWS_BUFFER (UINT64_C(1) << 1)

typedef enum ck_request_state
{
	CK_REQUEST_PENDING = 0,
	CK_REQUEST_RUNNING = 1,
	CK_REQUEST_CANCELLING = 2,
	CK_REQUEST_FAILING = 3,
	CK_REQUEST_COMPLETED = 4,
	CK_REQUEST_FAILED = 5,
	CK_REQUEST_STATE_INVALID = -1
} ck_request_state_t;

/* Process-local descriptor. Java never receives this struct or its pointers. */
typedef struct ck_request_descriptor
{
	uint32_t abi_version;
	uint32_t struct_size;
	uint64_t feature_flags;
	uint64_t ownership_flags;
	uint64_t owner_token;
	uint64_t lifetime_token;
	uint64_t request_id;
	const unsigned char *metadata;
	uint64_t metadata_length;
	const unsigned char *body;
	uint64_t body_length;
} ck_request_descriptor_t;

/* Atomic lifecycle state stays private to the C implementation layout. */
typedef struct ck_request
{
	ck_request_descriptor_t descriptor;
	_Atomic uint32_t state;
	ck_error_t error;
	unsigned char metadata_storage[CK_REQUEST_METADATA_STORAGE_BYTES];
} ck_request_t;

int ck_request_init(ck_request_t *request,
		const ck_request_descriptor_t *descriptor);
int ck_request_init_http(ck_request_t *request,
		const ck_http_request_t *http_request,
		const unsigned char *body,
		size_t body_length,
		uint64_t request_id,
		uint64_t owner_token,
		uint64_t lifetime_token);
int ck_request_begin(ck_request_t *request);
int ck_request_cancel(ck_request_t *request);
int ck_request_finish(ck_request_t *request);
ck_request_state_t ck_request_state(const ck_request_t *request);
int ck_request_fail(ck_request_t *request, const ck_error_t *error);
const ck_error_t *ck_request_error(const ck_request_t *request);

#endif
