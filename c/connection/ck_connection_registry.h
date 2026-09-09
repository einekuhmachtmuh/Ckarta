#ifndef CKARTA_CONNECTION_REGISTRY_H
#define CKARTA_CONNECTION_REGISTRY_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "ck_connection.h"

typedef uint64_t ck_connection_handle_t;
typedef struct ck_connection_registry_entry
{
	ck_connection_t connection;
	uint32_t generation;
	uint32_t reader_users;
	int active;
} ck_connection_registry_entry_t;

typedef struct ck_connection_registry
{
	pthread_mutex_t lock;
	ck_connection_registry_entry_t entries[CK_CONNECTION_REGISTRY_CAPACITY];
	int initialized;
} ck_connection_registry_t;

typedef struct ck_connection_registry_reader_pin
{
	ck_connection_t *connection;
	ck_http_connection_reader_t *reader;
	ck_connection_handle_t handle;
} ck_connection_registry_reader_pin_t;

int ck_connection_registry_init(ck_connection_registry_t *registry);
int ck_connection_registry_register(
		ck_connection_registry_t *registry,
		uint64_t connection_id,
		uint64_t request_id,
		uint64_t owner_token,
		uint64_t lifetime_token,
		ck_connection_handle_t *handle);
int ck_connection_registry_attach_socket(
		ck_connection_registry_t *registry,
		ck_connection_handle_t handle,
		uint64_t request_id,
		uint64_t owner_token,
		uint64_t lifetime_token,
		int socket_fd);
int ck_connection_registry_socket_fd(
		ck_connection_registry_t *registry,
		ck_connection_handle_t handle,
		uint64_t request_id,
		uint64_t owner_token,
		uint64_t lifetime_token);
int ck_connection_registry_start_async_cycle(
		ck_connection_registry_t *registry,
		ck_connection_handle_t handle,
		uint64_t request_id,
		uint64_t owner_token,
		uint64_t lifetime_token,
		uint64_t cycle_id);
int ck_connection_registry_try_terminal(
		ck_connection_registry_t *registry,
		ck_connection_handle_t handle,
		uint64_t request_id,
		uint64_t owner_token,
		uint64_t lifetime_token,
		uint64_t cycle_id,
		ck_connection_terminal_event_t event);
int ck_connection_registry_reader_acquire(
		ck_connection_registry_t *registry,
		ck_connection_handle_t handle,
		uint64_t request_id,
		uint64_t owner_token,
		uint64_t lifetime_token,
		ck_connection_registry_reader_pin_t *pin);
int ck_connection_registry_reader_release(
		ck_connection_registry_t *registry,
		ck_connection_registry_reader_pin_t *pin);
int ck_connection_registry_close(
		ck_connection_registry_t *registry,
		ck_connection_handle_t handle,
		uint64_t request_id,
		uint64_t owner_token,
		uint64_t lifetime_token);
int ck_connection_registry_retire(
		ck_connection_registry_t *registry,
		ck_connection_handle_t handle,
		uint64_t request_id,
		uint64_t owner_token,
		uint64_t lifetime_token);
int ck_connection_registry_active_count(ck_connection_registry_t *registry);
int ck_connection_registry_destroy(ck_connection_registry_t *registry);

#endif
