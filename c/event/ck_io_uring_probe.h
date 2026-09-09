#ifndef CKARTA_IO_URING_PROBE_H
#define CKARTA_IO_URING_PROBE_H

#include <stdint.h>

typedef struct ck_io_uring_probe_result
{
	int available;
	int error_number;
	uint32_t features;
	int fast_poll;
	int accept;
	int recv;
	int send;
} ck_io_uring_probe_result_t;

int ck_io_uring_probe(ck_io_uring_probe_result_t *result);

#endif
