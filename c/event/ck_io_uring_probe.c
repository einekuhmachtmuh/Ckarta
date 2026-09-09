#include "ck_io_uring_probe.h"

#ifdef __linux__

#include <errno.h>
#include <linux/io_uring.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

static int opcode_supported(
	const struct io_uring_probe *probe,
	unsigned int opcode)
{
	unsigned int i;

	if (probe == NULL)
	{
		return 0;
	}

	for (i = 0; i < probe->ops_len; ++i)
	{
		if (probe->ops[i].op == opcode)
		{
			return (probe->ops[i].flags & IO_URING_OP_SUPPORTED) != 0;
		}
	}
	return 0;
}

int ck_io_uring_probe(ck_io_uring_probe_result_t *result)
{
	struct io_uring_params params;
	struct io_uring_probe *probe;
	int fd;
	int error;

	if (result == NULL)
	{
		return EINVAL;
	}

	*result = (ck_io_uring_probe_result_t){0};
	params = (struct io_uring_params){0};

	fd = (int)syscall(SYS_io_uring_setup, 2u, &params);
	if (fd < 0)
	{
		error = errno;
		result->error_number = error;
		return 0;
	}

	result->available = 1;
	result->features = params.features;
	result->fast_poll = (params.features & IORING_FEAT_FAST_POLL) != 0;

	probe = calloc(1, sizeof(*probe)
			+ 256u * sizeof(struct io_uring_probe_op));
	if (probe == NULL)
	{
		error = errno;
		close(fd);
		result->error_number = error;
		return -1;
	}

	probe->ops_len = 256;
	if (syscall(SYS_io_uring_register, fd, IORING_REGISTER_PROBE,
			probe, 256u) == 0)
	{
		result->accept = opcode_supported(probe, IORING_OP_ACCEPT);
		result->recv = opcode_supported(probe, IORING_OP_RECV);
		result->send = opcode_supported(probe, IORING_OP_SEND);
#ifdef IORING_ACCEPT_MULTISHOT
		result->accept_multishot = result->accept;
#else
		result->accept_multishot = 0;
#endif
#ifdef IORING_RECV_MULTISHOT
		result->recv_multishot = result->recv;
#else
		result->recv_multishot = 0;
#endif
#ifdef IORING_REGISTER_PBUF_RING
		result->provided_buffer_ring = 1;
#else
		result->provided_buffer_ring = 0;
#endif
	}
	else
	{
		error = errno;
		result->error_number = error;
	}

	free(probe);
	if (close(fd) != 0 && result->error_number == 0)
	{
		result->error_number = errno;
	}
	return 0;
}

#else

#include <errno.h>

int ck_io_uring_probe(ck_io_uring_probe_result_t *result)
{
	if (result == NULL)
	{
		return EINVAL;
	}
	*result = (ck_io_uring_probe_result_t){0};
	result->error_number = ENOSYS;
	return 0;
}

#endif
