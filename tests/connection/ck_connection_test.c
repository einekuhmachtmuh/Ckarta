#include "../../c/connection/ck_connection.h"

#include <assert.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

struct race_args
{
	ck_connection_t *connection;
	ck_connection_terminal_event_t event;
	int result;
};

static void *terminal_thread(void *arg)
{
	struct race_args *args = arg;
	args->result = ck_connection_try_terminal(args->connection, args->event);
	return NULL;
}


static void test_http_keepalive_recycle(void)
{
	static const char requests[] =
			"GET /one HTTP/1.1\\r\\nHost: x\\r\\n\\r\\n"
			"GET /two HTTP/1.1\\r\\nHost: x\\r\\n\\r\\n";
	ck_connection_t connection;
	const ck_http_request_t *request;
	ck_http_connection_reader_t *reader;
	ck_http_response_t *response;
	ck_http_output_writer_t *writer;
	const unsigned char *body;
	size_t body_length;
	size_t consumed;
	int sockets[2];
	ck_http_connection_read_result_t read_result;

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
	assert(ck_connection_init(&connection, 3, 31, 32, 33) == 0);
	assert(ck_connection_attach_socket(&connection, sockets[0]) == 0);
	reader = ck_connection_http_reader(&connection);
	response = ck_connection_http_response(&connection);
	writer = ck_connection_http_writer(&connection);
	assert(reader != NULL);
	assert(response != NULL);
	assert(writer != NULL);

	assert(write(sockets[1], requests, sizeof(requests) - 1)
			== (ssize_t)(sizeof(requests) - 1));
	read_result = ck_http_connection_reader_drive(
			reader, sockets[0], NULL, NULL);
	assert(read_result == CK_HTTP_CONNECTION_READ_REQUEST_COMPLETE);
	request = ck_http_connection_reader_request(reader);
	assert(request != NULL);
	assert(request->target.length == 4);
	assert(memcmp(request->target.data, "/one", 4) == 0);
	assert(request->connection_close_required == 0);

	assert(ck_http_response_set_status(response, 200U) == 0);
	assert(ck_http_response_write_body(response, "ok", 2) == 0);
	assert(ck_http_response_finish(response) == 0);
	{
		unsigned char headers[CK_HTTP_RESPONSE_HEADER_BUFFER_BYTES];
		size_t header_length = 0;
		assert(ck_http_response_serialize_headers(
				response, headers, sizeof(headers), &header_length) == 0);
		assert(ck_http_output_writer_queue(
				writer, headers, header_length) == 0);
		assert(ck_http_output_writer_queue(writer, (const unsigned char *)"ok", 2) == 0);
	}
	assert(ck_http_output_writer_drive(writer)
			== CK_HTTP_OUTPUT_WRITE_DRAINED);
	assert(ck_connection_http_recycle(&connection) == 0);
	assert(ck_connection_http_response(&connection) != NULL);
	request = ck_http_connection_reader_request(reader);
	assert(request == NULL);

	read_result = ck_http_connection_reader_drive(
			reader, sockets[0], NULL, NULL);
	assert(read_result == CK_HTTP_CONNECTION_READ_REQUEST_COMPLETE);
	request = ck_http_connection_reader_request(reader);
	assert(request != NULL);
	assert(request->target.length == 4);
	assert(memcmp(request->target.data, "/two", 4) == 0);

	assert(ck_connection_try_terminal(
			&connection, CK_CONNECTION_TERMINAL_COMPLETE)
			== CK_CONNECTION_TERMINAL_CLAIMED);
	assert(ck_connection_close(&connection) == 0);
	assert(close(sockets[1]) == 0);
}

static void test_connection_close_prevents_recycle(void)
{
	static const char request[] =
			"GET / HTTP/1.1\\r\\nHost: x\\r\\n"
			"Connection: close\\r\\n\\r\\n";
	ck_connection_t connection;
	ck_http_connection_reader_t *reader;
	ck_http_response_t *response;
	ck_http_output_writer_t *writer;
	const ck_http_request_t *parsed;
	unsigned char headers[CK_HTTP_RESPONSE_HEADER_BUFFER_BYTES];
	size_t header_length = 0;
	int sockets[2];

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
	assert(ck_connection_init(&connection, 4, 41, 42, 43) == 0);
	assert(ck_connection_attach_socket(&connection, sockets[0]) == 0);
	reader = ck_connection_http_reader(&connection);
	response = ck_connection_http_response(&connection);
	writer = ck_connection_http_writer(&connection);
	assert(write(sockets[1], request, sizeof(request) - 1)
			== (ssize_t)(sizeof(request) - 1));
	assert(ck_http_connection_reader_drive(
			reader, sockets[0], NULL, NULL)
			== CK_HTTP_CONNECTION_READ_REQUEST_COMPLETE);
	parsed = ck_http_connection_reader_request(reader);
	assert(parsed != NULL && parsed->connection_close_required == 1);
	assert(ck_http_response_set_status(response, 200U) == 0);
	assert(ck_http_response_set_connection_close(response, 1) == 0);
	assert(ck_http_response_finish(response) == 0);
	assert(ck_http_response_serialize_headers(
			response, headers, sizeof(headers), &header_length) == 0);
	assert(ck_http_output_writer_queue(writer, headers, header_length) == 0);
	assert(ck_http_output_writer_drive(writer)
			== CK_HTTP_OUTPUT_WRITE_DRAINED);
	assert(ck_connection_http_recycle(&connection) == 3);
	assert(ck_connection_try_terminal(
			&connection, CK_CONNECTION_TERMINAL_COMPLETE)
			== CK_CONNECTION_TERMINAL_CLAIMED);
	assert(ck_connection_close(&connection) == 0);
	assert(close(sockets[1]) == 0);
}

int main(void)
{
	ck_connection_t connection;
	struct race_args complete = { 0 };
	struct race_args disconnect = { 0 };
	pthread_t complete_thread;
	pthread_t disconnect_thread;
	int socket_pair[2];
	char byte;
	int winners;

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, socket_pair) == 0);
	assert(ck_connection_init(&connection, 1, 11, 22, 33) == 0);
	assert(ck_connection_socket_fd(&connection) == -1);
	assert(ck_connection_http_reader(&connection) != NULL);
	assert(ck_connection_http_writer(&connection) != NULL);
	assert(ck_http_connection_reader_buffered_bytes(
			ck_connection_http_reader(&connection)) == 0);
	assert(ck_connection_attach_socket(&connection, socket_pair[0]) == 0);
	assert(ck_connection_socket_fd(&connection) == socket_pair[0]);
	assert(ck_connection_attach_socket(&connection, socket_pair[1]) == 1);
	assert(ck_connection_validate(&connection, 11, 22, 33) == 0);
	assert(ck_connection_validate(&connection, 11, 22, 34) == 1);
	assert(ck_connection_start_async_cycle(&connection, 42) == 0);
	assert(ck_connection_start_async_cycle(&connection, 42) == 1);
	assert(ck_connection_validate_cycle(&connection, 11, 22, 33, 42) == 0);
	assert(ck_connection_validate_cycle(&connection, 11, 22, 33, 43) == 1);
	assert(ck_connection_validate_cycle(&connection, 11, 22, 34, 42) == 1);

	assert(ck_connection_try_terminal(
			&connection, CK_CONNECTION_TERMINAL_COMPLETE)
			== CK_CONNECTION_TERMINAL_CLAIMED);
	assert(ck_connection_try_terminal(
			&connection, CK_CONNECTION_TERMINAL_COMPLETE)
			== CK_CONNECTION_TERMINAL_ALREADY_SAME);
	assert(ck_connection_try_terminal(
			&connection, CK_CONNECTION_TERMINAL_TIMEOUT)
			== CK_CONNECTION_TERMINAL_ALREADY_DIFFERENT);
	assert(ck_connection_close(&connection) == 0);
	assert(ck_connection_socket_fd(&connection) == -1);
	assert(ck_connection_http_reader(&connection) == NULL);
	assert(ck_connection_close(&connection) == 1);
	assert(ck_connection_state(&connection) == CK_CONNECTION_CLOSED);
	assert(recv(socket_pair[1], &byte, 1, 0) == 0);
	assert(close(socket_pair[1]) == 0);

	assert(ck_connection_init(&connection, 2, 21, 22, 23) == 0);
	assert(ck_connection_start_async(&connection) == 0);
	complete.connection = &connection;
	complete.event = CK_CONNECTION_TERMINAL_COMPLETE;
	disconnect.connection = &connection;
	disconnect.event = CK_CONNECTION_TERMINAL_CLIENT_DISCONNECT;
	assert(pthread_create(&complete_thread, NULL, terminal_thread, &complete) == 0);
	assert(pthread_create(&disconnect_thread, NULL, terminal_thread, &disconnect) == 0);
	assert(pthread_join(complete_thread, NULL) == 0);
	assert(pthread_join(disconnect_thread, NULL) == 0);
	winners = (complete.result == CK_CONNECTION_TERMINAL_CLAIMED)
			+ (disconnect.result == CK_CONNECTION_TERMINAL_CLAIMED);
	assert(winners == 1);
	assert((complete.result == CK_CONNECTION_TERMINAL_ALREADY_SAME)
			|| (complete.result == CK_CONNECTION_TERMINAL_ALREADY_DIFFERENT)
			|| complete.result == CK_CONNECTION_TERMINAL_CLAIMED);
	assert((disconnect.result == CK_CONNECTION_TERMINAL_ALREADY_SAME)
			|| (disconnect.result == CK_CONNECTION_TERMINAL_ALREADY_DIFFERENT)
			|| disconnect.result == CK_CONNECTION_TERMINAL_CLAIMED);
	assert(ck_connection_close(&connection) == 0);
	assert(ck_connection_http_reader(&connection) == NULL);
	return 0;
}