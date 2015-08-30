#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <ev.h>

#define DEBUG	1

#ifdef DEBUG
#define dbgprint(format, ...) printf(format, ##__VA_ARGS__)
#else
#define dbgprint(format, ...)
#endif

#define P2PUSER		"babylon"
#define toskaddr(addr)  (struct sockaddr *)(addr)

enum p2p_status {
	LOGIN_REQUEST,
	LOGIN_SUCCESS,
	TALK_TO,
};

struct p2p_client {
	int pfd;
	int status;
	struct ev_loop *loop;
	ev_io pio;
	ev_io pinput;
	ev_timer pto;	
	ev_timer ptick;
	struct sockaddr_in server;
	struct sockaddr_in local;
	struct sockaddr_in dest;
	char snbuf[1024];
	int snlen;
};

int p2p_client_flush(struct p2p_client *c);
int p2p_send_message(struct p2p_client *c, const char *key,
		     const char *format, ...);

static int get_sockaddr(char *buf, struct sockaddr_in *addr)
{

	char *ip, *port;
	char *p = buf;

	p = strchr(p, ' ');
	if (p == NULL)
		return -1;
	*p++ = 0;
	ip = p;
	p = strchr(p, ':');
	if (p == NULL)
		return -1;
	*p++ = 0;
	port = p;

	struct sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(atoi(port));
	if (inet_pton(AF_INET, ip, &sin.sin_addr) <= 0)
		return -1;

	*addr = sin;

	return 0;
}

void p2p_client_recv(struct ev_loop *loop, ev_io *w, int revents)
{
	struct p2p_client *c = w->data;
	char buf[1024];
	struct sockaddr_in addr;
	socklen_t socklen = sizeof(addr);
	ssize_t n;

again:
	n = recvfrom(c->pfd, buf, sizeof(buf), 0, toskaddr(&addr), &socklen);
	if (n == -1) {
		if (errno == EINTR)
			goto again;
		perror("recvfrom");
		exit(1);
	}

	assert(buf[n - 1] == '\n');
	buf[n-1] = 0;

	dbgprint("recv %s:%d %.*s\n", inet_ntoa(addr.sin_addr),
		 ntohs(addr.sin_port), (int)n, buf);

	char *p = strchr(buf, ':');
	if (p == NULL)
		return;

	*p++ = 0;
	while (*p == ' ')
		p++;

	struct sockaddr_in sin;
	char *user = p;
	if (strcmp(buf, "user-info") == 0) {
		if (get_sockaddr(p, &sin) == -1)
			return;

		c->snlen = snprintf(c->snbuf, sizeof(c->snbuf),
				    "talk-shake: %s\n", user);

		c->status = TALK_TO;
		c->dest = sin;

		sleep(1);
		p2p_client_flush(c);
		p2p_client_flush(c);
		sleep(1);
		p2p_client_flush(c);
	} else if (strcmp(buf, "open-channel") == 0) {
		if (get_sockaddr(p, &sin) == -1)
			return;

		p2p_send_message(c, "response", "success");
		p2p_send_message(c, "response", "success");

		c->snlen = snprintf(c->snbuf, sizeof(c->snbuf),
				    "talk-shake: %s\n", user);

		c->status = TALK_TO;
		c->dest = sin;

		p2p_client_flush(c);
		p2p_client_flush(c);
		sleep(1);
		p2p_client_flush(c);
	} else if (strcmp(buf, "talk-shake") == 0) {
		printf("p2p connection established\n");
	}
}

void p2p_user_input(struct ev_loop *loop, ev_io *w, int revents)
{
	struct p2p_client *c = w->data;

	fgets(c->snbuf, sizeof(c->snbuf), stdin);

	if (p2p_client_flush(c) < 0)
		perror("p2p_client_flush");
}

int p2p_client_init(struct ev_loop *loop, struct p2p_client *c,
		    const char *host, const char *service)
{
	int pfd;

	pfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (pfd == -1)
		return -1;

	/* bind a random port */
	srand(time(0));
	struct sockaddr_in sin;
again:
	sin.sin_family = AF_INET;
	sin.sin_port = htons(rand() % 20000 + 8196);
	sin.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(pfd, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
		if (errno == EADDRINUSE)
			goto again;
		else {
			close(pfd);
			return -1;
		}
	}

	struct addrinfo hints, *res;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	int e;

	e = getaddrinfo(host, service, &hints, &res);
	if (e != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(e));
		return -1;
	}

	c->server = *((struct sockaddr_in *)res->ai_addr);

	c->pfd = pfd;
	c->loop = loop;
	c->pinput.data = c;
	ev_io_init(&c->pinput, p2p_user_input, STDIN_FILENO, EV_READ); 
	ev_io_start(loop, &c->pinput);

	c->pio.data = c;
	ev_io_init(&c->pio, p2p_client_recv, pfd, EV_READ);
	ev_io_start(loop, &c->pio);

	return 0;
}

int send_request(int sockfd, struct sockaddr *addr, socklen_t socklen,
		 const char *key, const char *format, ...)
{		
	char line[1024];	
	int l;
	va_list ap;

	l = snprintf(line, sizeof(line), "%s: ", key);
	va_start(ap, format);
	l += vsnprintf(line + l, sizeof(line) - l, format, ap);
	va_end(ap);
	
	line[l++] = '\n';

	dbgprint("send %.*s", l, line);

	return sendto(sockfd, line, l, 0, addr, socklen);
}

int p2p_send_message(struct p2p_client *c, const char *key,
		     const char *format, ...)
{		
	char *line = c->snbuf;
	int buflen = sizeof(c->snbuf);
	int l;
	va_list ap;

	l = snprintf(line, buflen, "%s: ", key);
	va_start(ap, format);
	l += vsnprintf(line + l, buflen - l, format, ap);
	va_end(ap);
	
	line[l++] = '\n';
	line[l] = '\0';
	c->snlen = l;

	dbgprint("send %s:%d %.*s", inet_ntoa(c->dest.sin_addr),
		 ntohs(c->dest.sin_port), l, line);

	socklen_t socklen = sizeof(c->server);

	return sendto(c->pfd, line, l, 0, toskaddr(&c->server), socklen);
}

int p2p_client_flush(struct p2p_client *c)
{
	size_t l = strlen(c->snbuf);
	ssize_t n;
	socklen_t socklen;

	if (c->status == TALK_TO) {
		dbgprint("send %s:%d %.*s", inet_ntoa(c->dest.sin_addr),
			 ntohs(c->dest.sin_port), (int)l, c->snbuf);
		socklen = sizeof(c->dest);
		n = sendto(c->pfd, c->snbuf, l, 0, toskaddr(&c->dest), socklen);
	} else {
		dbgprint("send %s:%d %.*s", inet_ntoa(c->server.sin_addr),
			 ntohs(c->server.sin_port), (int)l, c->snbuf);
		socklen = sizeof(c->server);
		n = sendto(c->pfd, c->snbuf, l, 0, toskaddr(&c->server), socklen);
	}

	return n;
}


void p2p_timeout_retran(struct ev_loop *loop, ev_timer *w, int revents)
{
	struct p2p_client *c = w->data;

	p2p_client_flush(c);
}

int p2p_client_login(struct p2p_client *c, const char *host, const char *service)
{
	struct addrinfo hints, *res, *rp;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	int e;

	e = getaddrinfo(host, service, &hints, &res);
	if (e != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(e));
		return -1;
	}

	ssize_t n;
	for (rp = res; rp != NULL; rp = rp->ai_next) {
		c->server = *((struct sockaddr_in *)rp->ai_addr);
		n = p2p_send_message(c, "login-request", P2PUSER);
		if (n > 0)
			break;
	}

	if (rp == NULL)
		return -1;

	c->status = LOGIN_REQUEST;
	c->pto.data = c;
	ev_timer_init(&c->pto, p2p_timeout_retran, 2.0, 0.);
	ev_timer_start(c->loop, &c->pto);

	freeaddrinfo(res);

	return 0;
}

int main(int argc, char *argv[])
{
	struct ev_loop *loop;
	struct p2p_client p2pc;

	if (argc < 3) {
		fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
		return 1;
	}

	loop = ev_default_loop(0);
	if (loop == NULL)
		return 1;

	if (p2p_client_init(loop, &p2pc, argv[1], argv[2]) == -1)
		return 1;

	ev_run(loop, 0);

	return 0;	
}
