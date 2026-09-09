#include "ck_connection_registry.h"

#include <errno.h>
#include <string.h>

#define CK_CONNECTION_HANDLE_SLOT_MASK UINT64_C(0xffffffff)
#define CK_CONNECTION_HANDLE_GENERATION_SHIFT 32u

static ck_connection_handle_t ck_connection_registry_make_handle(
		size_t index, uint32_t generation)
{
	return ((uint64_t)generation << CK_CONNECTION_HANDLE_GENERATION_SHIFT)
			| ((uint64_t)index + UINT64_C(1));
}

static int ck_connection_registry_decode_handle(
		ck_connection_handle_t handle, size_t *index, uint32_t *generation)
{
	uint64_t slot;

	if (handle == 0 || index == NULL || generation == NULL)
	{
		return -1;
	}

	slot = handle & CK_CONNECTION_HANDLE_SLOT_MASK;
	if (slot == 0 || slot > CK_CONNECTION_REGISTRY_CAPACITY)
	{
		return -1;
	}

	*index = (size_t)(slot - 1U);
	*generation = (uint32_t)(handle >> CK_CONNECTION_HANDLE_GENERATION_SHIFT);
	return *generation == 0 ? -1 : 0;
}

static ck_connection_registry_entry_t *
ck_connection_registry_find(
		ck_connection_registry_t *registry,
		ck_connection_handle_t handle)
{
	size_t index;
	uint32_t generation;

	if (ck_connection_registry_decode_handle(handle, &index, &generation) != 0)
	{
		return NULL;
	}

	if (!registry->entries[index].active
			|| registry->entries[index].generation != generation)
	{
		return NULL;
	}

	return &registry->entries[index];
}

int ck_connection_registry_init(ck_connection_registry_t *registry)
{
	if (registry == NULL)
	{
		return -1;
	}

	memset(registry, 0, sizeof(*registry));
	if (pthread_mutex_init(&registry->lock, NULL) != 0)
	{
		return -1;
	}

	registry->initialized = 1;
	return 0;
}

int ck_connection_registry_register(
	ck_connection_registry_t *registry,
		uint64_t connection_id,
		uint64_t request_id,
		uint64_t owner_token,
		uint64_t lifetime_token,
		ck_connection_handle_t *handle)
{
	size_t index;
	ck_connection_registry_entry_t *entry;

	if (registry == NULL || !registry->initialized || handle == NULL
			|| connection_id == 0 || request_id == 0)
	{
		return -1;
	}

	if (pthread_mutex_lock(&registry->lock) != 0)
	{
		return -1;
	}

	for (index = 0; index < CK_CONNECTION_REGISTRY_CAPACITY; index++)
	{
		entry = &registry->entries[index];
		if (entry->active)
		{
			continue;
		}

		entry->generation++;
		if (entry->generation == 0)
		{
			entry->generation = 1;
		}
		entry->reader_users = 0;

		if (ck_connection_init(&entry->connection, connection_id,
				request_id, owner_token, lifetime_token) != 0)
		{
			(void)pthread_mutex_unlock(&registry->lock);
			return -1;
		}

		entry->active = 1;
		*handle = ck_connection_registry_make_handle(index, entry->generation);
		if (pthread_mutex_unlock(&registry->lock) != 0)
		{
			return -1;
		}
		return 0;
	}

	(void)pthread_mutex_unlock(&registry->lock);
	return ENOSPC;
}

int ck_connection_registry_attach_socket(
	ck_connection_registry_t *registry,
	ck_connection_handle_t handle,
	uint64_t request_id,
	uint64_t owner_token,
	uint64_t lifetime_token,
	int socket_fd)
{
	ck_connection_registry_entry_t *entry;
	int result;

	if (registry == NULL || !registry->initialized || socket_fd < 0)
	{
		return -1;
	}

	if (pthread_mutex_lock(&registry->lock) != 0)
	{
		return -1;
	}

	entry = ck_connection_registry_find(registry, handle);
	if (entry == NULL)
	{
		(void)pthread_mutex_unlock(&registry->lock);
		return -2;
	}

	if (ck_connection_validate(&entry->connection,
			request_id, owner_token, lifetime_token) != 0)
	{
		(void)pthread_mutex_unlock(&registry->lock);
		return -3;
	}

	result = ck_connection_attach_socket(&entry->connection, socket_fd);
	(void)pthread_mutex_unlock(&registry->lock);
	return result;
}

int ck_connection_registry_socket_fd(
	ck_connection_registry_t *registry,
	ck_connection_handle_t handle,
	uint64_t request_id,
	uint64_t owner_token,
	uint64_t lifetime_token)
{
	ck_connection_registry_entry_t *entry;
	int result;

	if (registry == NULL || !registry->initialized)
	{
		return -1;
	}

	if (pthread_mutex_lock(&registry->lock) != 0)
	{
		return -1;
	}

	entry = ck_connection_registry_find(registry, handle);
	if (entry == NULL)
	{
		(void)pthread_mutex_unlock(&registry->lock);
		return -2;
	}

	if (ck_connection_validate(&entry->connection,
			request_id, owner_token, lifetime_token) != 0)
	{
		(void)pthread_mutex_unlock(&registry->lock);
		return -3;
	}

	result = ck_connection_socket_fd(&entry->connection);
	(void)pthread_mutex_unlock(&registry->lock);
	return result;
}

int ck_connection_registry_start_async_cycle(
	ck_connection_registry_t *registry,
	ck_connection_handle_t handle,
	uint64_t request_id,
	uint64_t owner_token,
	uint64_t lifetime_token,
	uint64_t cycle_id)
{
	ck_connection_registry_entry_t *entry;
	int result;

	if (registry == NULL || !registry->initialized)
	{
		return -1;
	}

	if (pthread_mutex_lock(&registry->lock) != 0)
	{
		return -1;
	}

	entry = ck_connection_registry_find(registry, handle);
	if (entry == NULL)
	{
		(void)pthread_mutex_unlock(&registry->lock);
		return -2;
	}

	if (ck_connection_validate(&entry->connection,
			request_id, owner_token, lifetime_token) != 0)
	{
		(void)pthread_mutex_unlock(&registry->lock);
		return -3;
	}

	result = ck_connection_start_async_cycle(&entry->connection, cycle_id);
	(void)pthread_mutex_unlock(&registry->lock);
	return result;
}

int ck_connection_registry_try_terminal(
	ck_connection_registry_t *registry,
	ck_connection_handle_t handle,
	uint64_t request_id,
	uint64_t owner_token,
	uint64_t lifetime_token,
	uint64_t cycle_id,
	ck_connection_terminal_event_t event)
{
	ck_connection_registry_entry_t *entry;
	int result;

	if (registry == NULL || !registry->initialized)
	{
		return -1;
	}

	if (pthread_mutex_lock(&registry->lock) != 0)
	{
		return -1;
	}

	entry = ck_connection_registry_find(registry, handle);
	if (entry == NULL)
	{
		(void)pthread_mutex_unlock(&registry->lock);
		return -2;
	}

	if (ck_connection_validate_cycle(&entry->connection,
			request_id, owner_token, lifetime_token, cycle_id) != 0)
	{
		(void)pthread_mutex_unlock(&registry->lock);
		return -3;
	}

	result = ck_connection_try_terminal(&entry->connection, event);
	(void)pthread_mutex_unlock(&registry->lock);
	return result;
}

int ck_connection_registry_reader_acquire(
	ck_connection_registry_t *registry,
	ck_connection_handle_t handle,
	uint64_t request_id,
	uint64_t owner_token,
	uint64_t lifetime_token,
	ck_connection_registry_reader_pin_t *pin)
{
	ck_connection_registry_entry_t *entry;

	if (registry == NULL || !registry->initialized || pin == NULL)
	{
		return -1;
	}

	if (pin->connection != NULL || pin->reader != NULL || pin->handle != 0)
	{
		return -1;
	}

	if (pthread_mutex_lock(&registry->lock) != 0)
	{
		return -1;
	}

	entry = ck_connection_registry_find(registry, handle);
	if (entry == NULL)
	{
		(void)pthread_mutex_unlock(&registry->lock);
		return -2;
	}

	if (ck_connection_validate(&entry->connection,
			request_id, owner_token, lifetime_token) != 0)
	{
		(void)pthread_mutex_unlock(&registry->lock);
		return -3;
	}

	if (ck_connection_state(&entry->connection) == CK_CONNECTION_CLOSED
			|| entry->connection.http_reader == NULL)
	{
		(void)pthread_mutex_unlock(&registry->lock);
		return 1;
	}

	entry->reader_users++;
	pin->connection = &entry->connection;
	pin->reader = entry->connection.http_reader;
	pin->handle = handle;

	(void)pthread_mutex_unlock(&registry->lock);
	return 0;
}

int ck_connection_registry_reader_release(
	ck_connection_registry_t *registry,
	ck_connection_registry_reader_pin_t *pin)
{
	size_t index;
	uint32_t generation;
	ck_connection_registry_entry_t *entry;
	int result;

	if (registry == NULL || !registry->initialized || pin == NULL
			|| pin->connection == NULL || pin->reader == NULL
			|| pin->handle == 0)
	{
		return -1;
	}

	if (ck_connection_registry_decode_handle(
			pin->handle, &index, &generation) != 0)
	{
		return -1;
	}

	if (pthread_mutex_lock(&registry->lock) != 0)
	{
		return -1;
	}

	entry = &registry->entries[index];
	if (!entry->active || entry->generation != generation
			|| &entry->connection != pin->connection
			|| entry->connection.http_reader != pin->reader
			|| entry->reader_users == 0)
	{
		(void)pthread_mutex_unlock(&registry->lock);
		return -2;
	}

	entry->reader_users--;
	memset(pin, 0, sizeof(*pin));
	result = pthread_mutex_unlock(&registry->lock);
	return result;
}

int ck_connection_registry_close(
	ck_connection_registry_t *registry,
	ck_connection_handle_t handle,
	uint64_t request_id,
	uint64_t owner_token,
	uint64_t lifetime_token)
{
	ck_connection_registry_entry_t *entry;
	int result;

	if (registry == NULL || !registry->initialized)
	{
		return -1;
	}

	if (pthread_mutex_lock(&registry->lock) != 0)
	{
		return -1;
	}

	entry = ck_connection_registry_find(registry, handle);
	if (entry == NULL)
	{
		(void)pthread_mutex_unlock(&registry->lock);
		return -2;
	}

	if (ck_connection_validate(&entry->connection,
			request_id, owner_token, lifetime_token) != 0)
	{
		(void)pthread_mutex_unlock(&registry->lock);
		return -3;
	}

	if (entry->reader_users != 0)
	{
		(void)pthread_mutex_unlock(&registry->lock);
		return 2;
	}

	result = ck_connection_close(&entry->connection);
	(void)pthread_mutex_unlock(&registry->lock);
	return result;
}

int ck_connection_registry_retire(
	ck_connection_registry_t *registry,
	ck_connection_handle_t handle,
	uint64_t request_id,
	uint64_t owner_token,
	uint64_t lifetime_token)
{
	ck_connection_registry_entry_t *entry;
	int result;

	if (registry == NULL || !registry->initialized)
	{
		return -1;
	}

	if (pthread_mutex_lock(&registry->lock) != 0)
	{
		return -1;
	}

	entry = ck_connection_registry_find(registry, handle);
	if (entry == NULL)
	{
		(void)pthread_mutex_unlock(&registry->lock);
		return -2;
	}

	if (ck_connection_validate(&entry->connection,
			request_id, owner_token, lifetime_token) != 0)
	{
		(void)pthread_mutex_unlock(&registry->lock);
		return -3;
	}

	if (entry->reader_users != 0
			|| ck_connection_state(&entry->connection) != CK_CONNECTION_CLOSED
			|| ck_connection_socket_fd(&entry->connection) >= 0)
	{
		(void)pthread_mutex_unlock(&registry->lock);
		return 1;
	}

	entry->active = 0;
	result = pthread_mutex_unlock(&registry->lock);
	return result;
}

int ck_connection_registry_active_count(
	ck_connection_registry_t *registry)
{
	size_t i;
	int count = 0;

	if (registry == NULL || !registry->initialized)
	{
		return -1;
	}

	if (pthread_mutex_lock(&registry->lock) != 0)
	{
		return -1;
	}

	for (i = 0; i < CK_CONNECTION_REGISTRY_CAPACITY; i++)
	{
		if (registry->entries[i].active)
		{
			count++;
		}
	}

	(void)pthread_mutex_unlock(&registry->lock);
	return count;
}

int ck_connection_registry_destroy(ck_connection_registry_t *registry)
{
	if (registry == NULL || !registry->initialized)
	{
		return -1;
	}

	if (ck_connection_registry_active_count(registry) != 0)
	{
		return 1;
	}

	if (pthread_mutex_destroy(&registry->lock) != 0)
	{
		return -1;
	}

	registry->initialized = 0;
	return 0;
}
