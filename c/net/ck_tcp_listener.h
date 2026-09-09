#ifndef CKARTA_TCP_LISTENER_H
#define CKARTA_TCP_LISTENER_H

typedef struct ck_tcp_listener
{
	int socket_fd;
	int initialized;
} ck_tcp_listener_t;

int ck_tcp_listener_init(ck_tcp_listener_t *listener, unsigned short port);
int ck_tcp_listener_port(const ck_tcp_listener_t *listener);
int ck_tcp_listener_fd(const ck_tcp_listener_t *listener);
int ck_tcp_listener_accept(ck_tcp_listener_t *listener);
int ck_tcp_listener_destroy(ck_tcp_listener_t *listener);

#endif
