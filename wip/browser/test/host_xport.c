/* browser_transport_t over BSD sockets, for running the module on a Linux host. */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "host_xport.h"

static int udp_fd = -1, tcp_fd = -1, tcp_err, tcp_eof, tcp_connected;
static unsigned allowed[16];
static int n_allowed;
int host_udp_blocked;

void host_allow_udp(unsigned ip)
{
    if (n_allowed < 16)
        allowed[n_allowed++] = ip;
}

static int h_udp_send(void *c, unsigned ip, unsigned port, const void *d, unsigned n)
{
    struct sockaddr_in a;
    int i;
    (void)c;
    for (i = 0; i < n_allowed && allowed[i] != ip; i++)
        ;
    if (i == n_allowed) {
        host_udp_blocked++;
        return 0; /* pretend it left, as a lost datagram would */
    }
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(ip);
    return sendto(udp_fd, d, n, 0, (struct sockaddr *)&a, sizeof a) < 0 ? -1 : 0;
}

static void h_poll(void *c, unsigned now)
{
    static unsigned char buf[4096];
    int budget = 32; /* the same bound as net_poll */
    (void)c;
    while (budget--) {
        struct sockaddr_in a;
        socklen_t al = sizeof a;
        int n = recvfrom(udp_fd, buf, sizeof buf, 0, (struct sockaddr *)&a, &al);
        if (n < 0)
            break;
        browser_on_udp(ntohl(a.sin_addr.s_addr), ntohs(a.sin_port), buf, n, now);
    }
}

static int h_open(void *c, unsigned ip, unsigned port, unsigned now)
{
    struct sockaddr_in a;
    (void)c; (void)now;
    if (tcp_fd >= 0)
        return -1;
    tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    fcntl(tcp_fd, F_SETFL, O_NONBLOCK);
    tcp_err = tcp_eof = tcp_connected = 0;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(ip);
    if (connect(tcp_fd, (struct sockaddr *)&a, sizeof a) < 0 && errno != EINPROGRESS)
        tcp_err = errno;
    return 0;
}

static int h_status(void *c)
{
    (void)c;
    if (tcp_fd < 0 || tcp_err)
        return -1;
    if (!tcp_connected) {
        struct pollfd p = {tcp_fd, POLLOUT, 0};
        int e = 0;
        socklen_t l = sizeof e;
        if (poll(&p, 1, 0) <= 0)
            return HTTP_TCP_CONNECTING;
        getsockopt(tcp_fd, SOL_SOCKET, SO_ERROR, &e, &l);
        if (e) {
            tcp_err = e;
            return -1;
        }
        tcp_connected = 1;
    }
    return tcp_eof ? HTTP_TCP_PEER_DONE : HTTP_TCP_OPEN;
}

static int h_send(void *c, const void *d, unsigned n)
{
    int r = send(tcp_fd, d, n, MSG_NOSIGNAL);
    (void)c;
    if (r < 0) {
        if (errno == EAGAIN)
            return 0;
        tcp_err = errno;
        return -1;
    }
    return r;
}

static int h_recv(void *c, void *d, unsigned max)
{
    int r = recv(tcp_fd, d, max, 0);
    (void)c;
    if (r > 0)
        return r;
    if (r == 0) {
        tcp_eof = 1;
        return 0;
    }
    if (errno == EAGAIN)
        return 0;
    tcp_err = errno;
    return -1;
}

static void h_close(void *c)
{
    (void)c;
    if (tcp_fd >= 0)
        close(tcp_fd);
    tcp_fd = -1;
}

void host_xport_init(browser_transport_t *t, unsigned dns_ip)
{
    struct sockaddr_in a;

    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    fcntl(udp_fd, F_SETFL, O_NONBLOCK);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    bind(udp_fd, (struct sockaddr *)&a, sizeof a);
    memset(t, 0, sizeof *t);
    t->udp_send = h_udp_send;
    t->poll = h_poll;
    t->tcp.open = h_open;
    t->tcp.status = h_status;
    t->tcp.send = h_send;
    t->tcp.recv = h_recv;
    t->tcp.close = h_close;
    t->dns_ip = dns_ip;
}
