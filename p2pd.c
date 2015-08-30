/**
 * login: name
 * talk-to: name
 * get-user-list:
 *
 * open-channel: name addr
 *
 * response: status, description
 * user-list: name1 name2 ...
 * user-info: name addr
 */
 
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/util.h>

#define DEBUG	1

#ifdef DEBUG
#define dbgprint(format, ...) printf(format, ##__VA_ARGS__)
#else
#define dbgprint(format, ...)
#endif

#define toskaddr(addr)  (struct sockaddr *)(addr)

static void p2p_read(evutil_socket_t fd, short events, void *arg);
ssize_t send_data(int sockfd, char *buf, size_t len, struct sockaddr *addr,
		  socklen_t socklen);

struct user_info *user_list = NULL;
int p2pfd;

struct user_info {
        struct user_info *next;
        const char *name;
        struct sockaddr_in addr;
        int wait_ack;
        int retry;
        struct event *timer;
	int timeout;
        struct user_info *talk;
};

int p2p_serv_bind(struct in_addr *inaddr, unsigned short port)
{
        int sock;
        
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock == -1)
                return -1;
                
        struct sockaddr_in sin;
        sin.sin_family = AF_INET;
        sin.sin_port = htons(port);
        if (inaddr)
                sin.sin_addr = *inaddr;
        else
                sin.sin_addr.s_addr = htonl(INADDR_ANY);
                
        if (bind(sock, (struct sockaddr *)&sin, sizeof(sin)) == -1)
                return -1;
                
        return sock;
}

int main(int argc, char *argv[])
{
        struct event_base *base;
        int port = 8800;
        
        if (argc > 1)
                port = atoi(argv[1]);
                
        if (port <= 0 || port > 65535) {
                fprintf(stderr, "invalid port %d\n", port);
                return 1;
        }
        
        base = event_base_new();
        if (base == NULL)
                return 1;
        
        p2pfd = p2p_serv_bind(NULL, port);
        if (p2pfd == -1)
                return 1;
                
        struct event *p2p;
        
        p2p = event_new(base, p2pfd, EV_READ | EV_PERSIST, p2p_read, base);
        if (p2p == NULL)
                return 1;
                
        event_add(p2p, NULL);
        
        event_base_dispatch(base);
        
        return 0;
}

#define BUFLINE  4095

struct user_info **find_user(const char *name)
{
        struct user_info **pp = &user_list;
        
        for (; *pp != NULL; pp = &(*pp)->next) {
                if (strcmp((*pp)->name, name) == 0)
                        break;
        }
        
        return pp;
}

struct user_info **find_user_by_addr(struct sockaddr *addr, socklen_t socklen)
{
        struct user_info **pp = &user_list;
        struct sockaddr_in *saddr = (void *)addr;
        
        for (; *pp != NULL; pp = &(*pp)->next) {
                if (saddr->sin_addr.s_addr == (*pp)->addr.sin_addr.s_addr &&
                    saddr->sin_port == (*pp)->addr.sin_port)
                        break;
        }
        
        return pp;
}

int find_and_add_user(const char *name, struct sockaddr_in *addr)
{
        struct user_info **u = find_user(name);
        if (*u != NULL) {
		(*u)->addr = *addr;
		return 0;
	}
        *u = calloc(1, sizeof(struct user_info));
        if (*u == NULL)
                return -1;
                
        (*u)->next = NULL;
        (*u)->name = strdup(name);
        (*u)->addr = *addr;
        
        return 0;
}

int send_response(int sockfd, struct sockaddr *addr, socklen_t socklen,
                   const char *status, const char *message)
{
        char line[1024];
        int l;
        
        l = snprintf(line, sizeof(line), "response: %s", status);
        if (message != NULL)
                l += snprintf(line + l, sizeof(line) - l, " %s\n", message);
        else
                l += snprintf(line + l, sizeof(line) - l, "\n");
        
        return send_data(sockfd, line, l, addr, socklen);
}

#define tosaddr(addr)	(struct sockaddr_in *)(addr)

ssize_t send_data(int sockfd, char *buf, size_t len, struct sockaddr *addr,
		  socklen_t socklen)
{
	int n;

	for (;;) {
		n = sendto(sockfd, buf, len, 0, toskaddr(addr), socklen);
		if (n == -1) {
			if (errno == EINTR)	
				continue;
			break;
		}
		dbgprint("send %s:%d %.*s", inet_ntoa((tosaddr(addr))->sin_addr),
			 ntohs((tosaddr(addr))->sin_port), (int)len, buf);
		break;
	}

	return n;
}

int send_user_list(int sockfd, struct sockaddr *addr, socklen_t socklen)
{
        struct user_info *pos;
        size_t bs = 4096;
	ssize_t n;
        char *b = malloc(bs);
        if (b == NULL)
                return -1;
                
        int l, sv;
        
        l = snprintf(b, bs, "user-list:");
        
        for (pos = user_list; pos != NULL; pos = pos->next) {
                sv = l;
                l += snprintf(b + l, bs - l, " %s", pos->name);
                if (l < bs)
                        continue;
                
                l = sv;
                bs += 4096;    
                b = realloc(b, bs);
                if (b == NULL)
                        return -1;
                l += snprintf(b + l, bs - l, " %s", pos->name);
        }
        
        l += snprintf(b + l, bs - l, "\n");
        
        n = send_data(sockfd, b, l, addr, socklen);
	free(b);

	return n;
}

int send_user_info(int sockfd, struct sockaddr *addr, socklen_t socklen,
                   struct user_info *user)
{
        char line[128];
        int l;
        
        l = snprintf(line, sizeof(line), "user-info: %s %s:%d\n", user->name,
                     inet_ntoa(user->addr.sin_addr),
                     ntohs(user->addr.sin_port));
                     
        return send_data(sockfd, line, l, addr, socklen);
}

int send_open_channel(int skfd, struct user_info *dest, struct user_info *orig)
{
        char line[128];
	int l;
        
        l = snprintf(line, sizeof(line), "open-channel: %s %s:%d\n", orig->name,
                     inet_ntoa(orig->addr.sin_addr),
                     ntohs(orig->addr.sin_port));
        
        dest->wait_ack = 1;
        dest->talk = orig;
                     
	socklen_t socklen = sizeof(dest->addr);

        return send_data(skfd, line, l, toskaddr(&dest->addr), socklen);
}

void response_timeout_retran(evutil_socket_t fd, short event, void *arg)
{
        struct user_info *dest, *orig;
        
        dest = arg;
        orig = dest->talk;
        
        if (++dest->retry > 3) {
		dest->retry = 0;
                event_free(dest->timer);
                dest->timer = NULL;
                send_response(p2pfd, toskaddr(&orig->addr), sizeof(orig->addr),
			      "error", "dest not responsed");
                return;
        }
        
        send_open_channel(p2pfd, dest, orig);
        dest->retry++;
	struct timeval tv;
	tv.tv_sec = dest->timeout / 1000;
	tv.tv_usec = dest->timeout % 1000 * 1000;
	event_add(dest->timer, &tv);
}

int response_timeout_handle(struct event_base *base,
			    struct user_info *user, int timeout)
{
        struct timeval tv;
        
        assert(user->timer == NULL);
        user->timer = event_new(base, -1, 0, response_timeout_retran, user);
	if (user->timer == NULL)
		return -1;

        tv.tv_sec = timeout / 1000;
        tv.tv_usec = timeout % 1000 * 1000;
        event_add(user->timer, &tv);
        user->wait_ack = 1;
	user->timeout = timeout;

	return 0;
}

#define BUFLEN	4095

static void p2p_read(evutil_socket_t fd, short events, void *arg)
{
        char buf[BUFLEN+1];
        ssize_t n;
        struct sockaddr_in claddr;
        socklen_t sklen;
        struct event_base *base = arg;
        char *p;
        
again:
	sklen = sizeof(claddr);
        n = recvfrom(fd, buf, BUFLINE, 0, toskaddr(&claddr), &sklen);
        if (n == -1) {
                if (errno == EINTR)
                        goto again;
                return;
        }

        if (buf[n-1] != '\n')
                return;

        buf[n-1] = '\0';

        p = strchr(buf, ':');
	if (p == NULL)
		return;

	dbgprint("recv %s:%d %s\n", inet_ntoa(claddr.sin_addr),
		 ntohs(claddr.sin_port), buf);

        *p++ = 0;
        
        while (*p == ' ')
                p++;
        
        struct user_info **pp, *user;
        if (strcmp(buf, "login") == 0) {
                find_and_add_user(p, &claddr);
                send_response(fd, toskaddr(&claddr), sklen, "success", NULL);
        } else if (strcmp(buf, "get-user-list") == 0) {
                pp = find_user_by_addr(toskaddr(&claddr), sklen);
                if (*pp == NULL)
                        send_response(fd, toskaddr(&claddr), sklen, "error",
                                      "not logined user");
                else
                        send_user_list(fd, toskaddr(&claddr), sklen);
        } else if (strcmp(buf, "talk-to") == 0) {
                pp = find_user_by_addr(toskaddr(&claddr), sklen);
                if (*pp == NULL) {
                        send_response(fd, toskaddr(&claddr), sklen, "error",
                                      "not logined user");
                        return;
                }
                user = *pp;
                pp = find_user(p);
                if (*pp == NULL)
                        send_response(fd, toskaddr(&claddr), sklen, "error",
                                      "user is not online");
                else {
                        send_user_info(fd, toskaddr(&claddr), sklen, *pp);
                        send_open_channel(fd, *pp, user);
                        response_timeout_handle(base, *pp, 4000);
                }
        } else if (strcmp(buf, "response") == 0) {
                pp = find_user_by_addr(toskaddr(&claddr), sklen);
                if (*pp == NULL) {
                        send_response(fd, toskaddr(&claddr), sklen, "error",
                                      "not logined user");
                        return;
                }
                
                if ((*pp)->wait_ack) {
                        evtimer_del((*pp)->timer);
                        event_free((*pp)->timer);
                        (*pp)->timer = NULL;
                        (*pp)->wait_ack = 0;
                        return;
                }
        } else {
                send_response(fd, toskaddr(&claddr), sklen, "error",
                              "unknown message");
      }
}
