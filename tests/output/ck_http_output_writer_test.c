#define _GNU_SOURCE

#include "../../c/event/ck_event_loop.h"
#include "../../c/output/ck_http_output_writer.h"

#include <assert.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
	ck_event_loop_t loop = {0};
	ck_event_notification_t notifications[2] = {0};
	ck_http_output_writer_t writer;
	unsigned char payload[CK_HTTP_OUTPUT_WRITE_BUFFER_BYTES];
	unsigned char received[CK_HTTP_OUTPUT_WRITE_BUFFER_BYTES];
	int sockets[2];
	int count;
	size_t received_length = 0;
	ck_http_output_write_result_t result;

	memset(payload, 'x', sizeof(payload));
	memset(received, 0, sizeof(received));
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
	assert(ck_event_loop_init(&loop) == 0);
	ck_http_output_writer_init(&writer);
	assert(ck_http_output_writer_attach_socket(&writer, sockets[0]) == 0);
	assert(ck_http_output_writer_queue(&writer, payload, sizeof(payload)) == 0);

	result = ck_http_output_writer_drive(&writer);
	assert(result == CK_HTTP_OUTPUT_WRITE_NEED_WRITE);
	assert(ck_http_output_writer_buffered_bytes(&writer)
			== sizeof(payload) - CK_HTTP_OUTPUT_WRITE_BUDGET_BYTES);

	count = (int)recv(sockets[1], received, sizeof(received), 0);
	assert(count == (int)CK_HTTP_OUTPUT_WRITE_BUDGET_BYTES);
	assert(memcmp(received, payload, CK_HTTP_OUTPUT_WRITE_BUDGET_BYTES) == 0);

	assert(ck_event_loop_add(&loop, sockets[0], UINT64_C(0xfeed),
			CK_EVENT_WRITE | CK_EVENT_ERROR | CK_EVENT_RDHUP) == 0);
	count = ck_event_loop_wait(&loop, notifications, 2, 1000);
	assert(count == 1);
	assert(notifications[0].cookie == UINT64_C(0xfeed));
	assert((notifications[0].events & CK_EVENT_WRITE) != 0);

	result = ck_http_output_writer_drive(&writer);
	assert(result == CK_HTTP_OUTPUT_WRITE_DRAINED);
	assert(ck_http_output_writer_buffered_bytes(&writer) == 0);

	received_length = 0;
	while (received_length < sizeof(payload) - CK_HTTP_OUTPUT_WRITE_BUDGET_BYTES)
	{
		ssize_t read_length = recv(sockets[1],
				received + received_length,
				sizeof(received) - received_length,
				0);
		assert(read_length > 0);
		received_length += (size_t)read_length;
	}
	assert(memcmp(received,
		payload + CK_HTTP_OUTPUT_WRITE_BUDGET_BYTES,
		sizeof(payload) - CK_HTTP_OUTPUT_WRITE_BUDGET_BYTES) == 0);

	assert(ck_event_loop_remove(&loop, sockets[0]) == 0);
	assert(close(sockets[1]) == 0);
	assert(ck_http_output_writer_drive(&writer)
			== CK_HTTP_OUTPUT_WRITE_DRAINED);
	assert(close(sockets[0]) == 0);
	assert(ck_event_loop_destroy(&loop) == 0);

	return 0;
}
