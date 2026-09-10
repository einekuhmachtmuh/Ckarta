#ifndef CKARTA_TCP_LISTENER_H
#define CKARTA_TCP_LISTENER_H

#include "../platform/ck_socket.h"

typedef struct ck_tcp_listener
{
	ck_socket_t socket;
	int initialized;
} ck_tcp_listener_t;

int ck_tcp_listener_init(ck_tcp_listener_t *listener, unsigned short port);
int ck_tcp_listener_port(const ck_tcp_listener_t *listener);
ck_socket_t ck_tcp_listener_socket(const ck_tcp_listener_t *listener);
int ck_tcp_listener_accept(ck_tcp_listener_t *listener,
	ck_socket_t *accepted_socket);
int ck_tcp_listener_destroy(ck_tcp_listener_t *listener);

#endif
