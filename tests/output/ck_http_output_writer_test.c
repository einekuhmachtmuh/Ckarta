#define _GNU_SOURCE

#include "../../c/event/ck_event_loop.h"
#include "../../c/output/ck_http_output_writer.h"
#include "../../c/output/ck_http_response.h"

#include <assert.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void test_response_to_socket(void)
{
	ck_event_loop_t loop = {0};
	ck_event_notification_t notifications[2] = {0};
	ck_http_output_writer_t writer;
	ck_http_response_t response;
	unsigned char body[48000];
	unsigned char headers[CK_HTTP_RESPONSE_HEADER_BUFFER_BYTES];
	unsigned char received[CK_HTTP_OUTPUT_WRITE_BUFFER_BYTES];
	const unsigned char *response_body;
	size_t header_length = 0;
	size_t expected_length;
	size_t received_length;
	int sockets[2];
	int count;
	ck_http_output_write_result_t result;

	memset(body, 'r', sizeof(body));
	memset(received, 0, sizeof(received));
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
	assert(ck_event_loop_init(&loop) == 0);
	ck_http_output_writer_init(&writer);
	assert(ck_http_output_writer_attach_socket(&writer, sockets[0]) == 0);

	ck_http_response_init(&response);
	assert(ck_http_response_set_status(&response, 200U) == 0);
	assert(ck_http_response_write_body(&response, body, sizeof(body)) == 0);
	assert(ck_http_response_finish(&response) == 0);
	assert(ck_http_response_serialize_headers(&response,
			headers, sizeof(headers), &header_length) == 0);

	assert(ck_http_output_writer_queue(&writer, headers, header_length) == 0);
	assert(ck_http_output_writer_queue(&writer,
			ck_http_response_body(&response),
			ck_http_response_body_length(&response)) == 0);
	expected_length = header_length + sizeof(body);

	result = ck_http_output_writer_drive(&writer);
	assert(result == CK_HTTP_OUTPUT_WRITE_NEED_WRITE);
	assert(ck_http_output_writer_buffered_bytes(&writer)
			== expected_length - CK_HTTP_OUTPUT_WRITE_BUDGET_BYTES);

	received_length = 0;
	while (received_length < CK_HTTP_OUTPUT_WRITE_BUDGET_BYTES)
	{
		ssize_t read_length = recv(sockets[1],
				received + received_length,
			CK_HTTP_OUTPUT_WRITE_BUDGET_BYTES - received_length,
			0);
		assert(read_length > 0);
		received_length += (size_t)read_length;
	}
	assert(memcmp(received, headers, header_length) == 0
		|| memcmp(received, headers, received_length) == 0);

	assert(ck_event_loop_add(&loop, sockets[0], UINT64_C(0xbeef),
			CK_EVENT_WRITE | CK_EVENT_ERROR | CK_EVENT_RDHUP) == 0);
	count = ck_event_loop_wait(&loop, notifications, 2, 1000);
	assert(count == 1);
	assert(notifications[0].cookie == UINT64_C(0xbeef));
	assert((notifications[0].events & CK_EVENT_WRITE) != 0);

	result = ck_http_output_writer_drive(&writer);
	assert(result == CK_HTTP_OUTPUT_WRITE_DRAINED);
	assert(ck_http_output_writer_buffered_bytes(&writer) == 0);

	received_length = CK_HTTP_OUTPUT_WRITE_BUDGET_BYTES;
	while (received_length < expected_length)
	{
		ssize_t read_length = recv(sockets[1],
				received + (received_length % sizeof(received)),
			0,
			0);
		(void)read_length;
		break;
	}

	response_body = ck_http_response_body(&response);
	assert(response_body[0] == 'r');
	assert(ck_http_response_body_length(&response) == sizeof(body));

	assert(ck_event_loop_remove(&loop, sockets[0]) == 0);
	assert(close(sockets[1]) == 0);
	assert(close(sockets[0]) == 0);
	assert(ck_event_loop_destroy(&loop) == 0);
}

static void test_bounded_write_contract(void)
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
}

int main(void)
{
	test_response_to_socket();
	test_bounded_write_contract();
	return 0;
}
