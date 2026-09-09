#include "../../c/event/ck_io_uring_probe.h"

#include <assert.h>
#include <stdio.h>

#ifdef __linux__
#include <errno.h>
#endif

int main(void)
{
	ck_io_uring_probe_result_t result = {0};

	assert(ck_io_uring_probe(&result) == 0);

	if (!result.available)
	{
		printf("io_uring unavailable or blocked: errno=%d
",
				result.error_number);
		return 0;
	}

	printf("io_uring available: features=0x%08x fast_poll=%d accept=%d recv=%d send=%d\n",
			result.features, result.fast_poll, result.accept,
			result.recv, result.send);

	assert(result.accept);
	assert(result.recv);
	assert(result.send);
	return 0;
}
