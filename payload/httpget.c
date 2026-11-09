#include "httpget.h"
#include "util.h"

#define CONNECT_MS 10000
#define IDLE_MS    10000
#define TOTAL_MS   30000
#define MAX_BODY   (256 * 1024)

static unsigned put_s(unsigned char *d, unsigned off, const char *s)
{
    unsigned n = strlen(s);
    memcpy(d + off, s, n);
    return off + n;
}

static unsigned put_u(unsigned char *d, unsigned off, unsigned v)
{
    char b[10];
    int i = 0;
    do { b[i++] = '0' + v % 10; v /= 10; } while (v);
    while (i) d[off++] = b[--i];
    return off;
}

int http_start(http_t *h, const http_tcp_t *tcp, unsigned ip, unsigned port,
               const char *host, const char *path, http_sink_fn sink, void *user,
               unsigned now)
{
    unsigned off = 0;

    if (strlen(host) + strlen(path) + 96 > HTTP_REQ_MAX)
        return -1;
    memset(h, 0, sizeof *h);
    h->tcp = tcp; h->sink = sink; h->user = user;
    h->connect_ms = CONNECT_MS; h->idle_ms = IDLE_MS;
    h->total_ms = TOTAL_MS; h->max_body = MAX_BODY;
    off = put_s(h->req, off, "GET ");
    off = put_s(h->req, off, path);
    off = put_s(h->req, off, " HTTP/1.0\r\nHost: ");
    off = put_s(h->req, off, host);
    if (port != 80) {
        h->req[off++] = ':';
        off = put_u(h->req, off, port);
    }
    off = put_s(h->req, off, "\r\nUser-Agent: samp-ps2\r\nAccept: */*\r\nConnection: close\r\n\r\n");
    h->req_len = off;
    h->started = h->last_rx = now;
    if (tcp->open(tcp->ctx, ip, port, now) < 0) {
        h->state = HTTP_FAILED;
        h->error = HTTP_E_CONNECT;
        return 0;
    }
    h->state = HTTP_CONNECTING;
    return 0;
}

static void finish(http_t *h, http_state_t st, http_error_t e)
{
    h->tcp->close(h->tcp->ctx);
    h->state = st;
    h->error = e;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* Returns the text after "name:" with spaces skipped, or 0 if another header. */
static const char *header_value(const char *line, const char *name)
{
    while (*name) {
        if (lower(*line) != *name)
            return 0;
        line++; name++;
    }
    if (*line++ != ':')
        return 0;
    while (*line == ' ' || *line == '\t')
        line++;
    return line;
}

static int has_word(const char *s, const char *w)
{
    unsigned n = strlen(w), i;
    for (; *s; s++) {
        for (i = 0; i < n && lower(s[i]) == w[i]; i++)
            ;
        if (i == n)
            return 1;
    }
    return 0;
}

/* Handles one complete header line. Returns 1 at the end of the headers. */
static int header_line(http_t *h)
{
    const char *v;

    h->line[h->line_len] = 0;
    if (h->line_no++ == 0) {
        const char *s = h->line;
        if (h->line_len < 12 || memcmp(s, "HTTP/1.", 7) || s[8] != ' ' ||
            s[9] < '0' || s[9] > '9' || s[10] < '0' || s[10] > '9' ||
            s[11] < '0' || s[11] > '9') {
            finish(h, HTTP_FAILED, HTTP_E_BAD_RESPONSE);
            return 0;
        }
        h->status = (s[9] - '0') * 100 + (s[10] - '0') * 10 + (s[11] - '0');
        return 0;
    }
    if (h->line_len == 0)
        return 1;
    if ((v = header_value(h->line, "content-length")) != 0) {
        unsigned n = 0;
        if (*v < '0' || *v > '9') {
            finish(h, HTTP_FAILED, HTTP_E_BAD_RESPONSE);
            return 0;
        }
        for (; *v >= '0' && *v <= '9'; v++) {
            if (n > (0xFFFFFFFFu - 9) / 10) { /* larger than we would ever accept */
                finish(h, HTTP_FAILED, HTTP_E_TOO_BIG);
                return 0;
            }
            n = n * 10 + (*v - '0');
        }
        h->have_len = 1;
        h->content_len = n;
    } else if ((v = header_value(h->line, "transfer-encoding")) != 0) {
        if (has_word(v, "chunked")) {
            finish(h, HTTP_FAILED, HTTP_E_CHUNKED);
            return 0;
        }
    }
    return 0;
}

static void body_bytes(http_t *h, const unsigned char *d, unsigned n)
{
    if (h->have_len && n > h->content_len - h->body_got)
        n = h->content_len - h->body_got; /* ignore anything past the declared end */
    if (n > h->max_body - h->body_got) {
        finish(h, HTTP_FAILED, HTTP_E_TOO_BIG);
        return;
    }
    h->body_got += n;
    if (n && h->sink(h->user, d, n) < 0) {
        finish(h, HTTP_FAILED, HTTP_E_SINK);
        return;
    }
    if (h->have_len && h->body_got == h->content_len)
        finish(h, HTTP_DONE, HTTP_E_NONE);
}

/* Header parsing, then hands whatever follows them to the body. */
static void feed(http_t *h, const unsigned char *d, unsigned n)
{
    unsigned i = 0;

    while (h->state == HTTP_HEADERS && i < n) {
        unsigned char c = d[i++];
        if (c == '\n') {
            if (h->line_len && h->line[h->line_len - 1] == '\r')
                h->line_len--;
            if (header_line(h) && h->state == HTTP_HEADERS) {
                if (h->status != 200) {
                    finish(h, HTTP_FAILED, HTTP_E_STATUS);
                    return;
                }
                if (h->have_len && h->content_len > h->max_body) {
                    finish(h, HTTP_FAILED, HTTP_E_TOO_BIG);
                    return;
                }
                h->state = HTTP_BODY;
                if (h->have_len && h->content_len == 0)
                    finish(h, HTTP_DONE, HTTP_E_NONE);
            }
            h->line_len = 0;
        } else if (h->line_len < HTTP_LINE_MAX - 1) {
            h->line[h->line_len++] = c; /* a longer line keeps its start, all we read */
        }
    }
    if (h->state == HTTP_BODY && i < n)
        body_bytes(h, d + i, n - i);
}

http_state_t http_tick(http_t *h, unsigned now)
{
    static unsigned char buf[1024];
    const http_tcp_t *t = h->tcp;
    unsigned budget = HTTP_READ_PER_TICK;
    int st;

    if (h->state == HTTP_IDLE || h->state == HTTP_DONE || h->state == HTTP_FAILED)
        return h->state;

    if ((int)(now - h->started) > (int)h->total_ms) {
        finish(h, HTTP_FAILED, HTTP_E_TIMEOUT);
        return h->state;
    }
    if (h->state == HTTP_CONNECTING) {
        st = t->status(t->ctx);
        if (st < 0) {
            finish(h, HTTP_FAILED, HTTP_E_CONNECT);
            return h->state;
        }
        if (st == HTTP_TCP_CONNECTING) {
            if ((int)(now - h->started) > (int)h->connect_ms)
                finish(h, HTTP_FAILED, HTTP_E_CONNECT);
            return h->state;
        }
        h->state = HTTP_SENDING;
        h->last_rx = now;
    }
    if (h->state == HTTP_SENDING) {
        int n = t->send(t->ctx, h->req + h->req_off, h->req_len - h->req_off);
        if (n < 0) {
            finish(h, HTTP_FAILED, HTTP_E_RESET);
            return h->state;
        }
        h->req_off += n;
        if (h->req_off == h->req_len)
            h->state = HTTP_HEADERS;
    }
    /* Drain before believing status: data can be queued behind a FIN or RST. */
    while (budget && (h->state == HTTP_HEADERS || h->state == HTTP_BODY)) {
        unsigned want = budget < sizeof buf ? budget : sizeof buf;
        int n = t->recv(t->ctx, buf, want);
        if (n <= 0)
            break;
        h->last_rx = now;
        budget -= n;
        feed(h, buf, n);
    }
    if ((h->state != HTTP_HEADERS && h->state != HTTP_BODY) || !budget)
        return h->state;

    st = t->status(t->ctx);
    if (st < 0) {
        finish(h, HTTP_FAILED, HTTP_E_RESET);
    } else if (st == HTTP_TCP_PEER_DONE) {
        if (h->state == HTTP_HEADERS || (h->have_len && h->body_got < h->content_len))
            finish(h, HTTP_FAILED, HTTP_E_TRUNCATED);
        else
            finish(h, HTTP_DONE, HTTP_E_NONE); /* no Content-Length: FIN is the end marker */
    } else if ((int)(now - h->last_rx) > (int)h->idle_ms) {
        finish(h, HTTP_FAILED, HTTP_E_TIMEOUT);
    }
    return h->state;
}
