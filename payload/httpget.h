#pragma once
/* HTTP/1.0 GET as a polled state machine over injected TCP functions. The body
 * is streamed to a sink a piece at a time, so its size costs no memory here.
 * Plain HTTP only: the EE has no TLS, so a redirect to https is a failure. */

/* One connection at a time. All calls return immediately. */
typedef struct {
    void *ctx;
    int  (*open)(void *ctx, unsigned ip, unsigned port, unsigned now_ms); /* 0, or <0 */
    int  (*status)(void *ctx);  /* HTTP_TCP_*, or <0 if failed */
    int  (*send)(void *ctx, const void *d, unsigned n); /* bytes taken, or <0 */
    int  (*recv)(void *ctx, void *d, unsigned max);     /* bytes read, 0 if none, or <0 */
    void (*close)(void *ctx);
} http_tcp_t;

#define HTTP_TCP_CONNECTING 0
#define HTTP_TCP_OPEN       1
#define HTTP_TCP_PEER_DONE  2 /* peer sent a clean FIN; recv may still return data */

typedef enum {
    HTTP_IDLE, HTTP_CONNECTING, HTTP_SENDING, HTTP_HEADERS, HTTP_BODY, HTTP_DONE, HTTP_FAILED
} http_state_t;

typedef enum {
    HTTP_E_NONE, HTTP_E_CONNECT, HTTP_E_TIMEOUT, HTTP_E_RESET, HTTP_E_BAD_RESPONSE,
    HTTP_E_STATUS, HTTP_E_CHUNKED, HTTP_E_TRUNCATED, HTTP_E_TOO_BIG, HTTP_E_SINK
} http_error_t;

#define HTTP_REQ_MAX  384
#define HTTP_LINE_MAX 128
#define HTTP_READ_PER_TICK 4096 /* bounds the parse work done in one game frame */

/* Returns 0 to carry on, <0 to abort the transfer. */
typedef int (*http_sink_fn)(void *user, const unsigned char *d, unsigned n);

typedef struct {
    const http_tcp_t *tcp;
    http_sink_fn sink;
    void *user;
    http_state_t state;
    http_error_t error;
    int status;                 /* HTTP status code once known */
    unsigned started, last_rx;
    unsigned connect_ms, idle_ms, total_ms, max_body;
    unsigned char req[HTTP_REQ_MAX];
    unsigned req_len, req_off;
    char line[HTTP_LINE_MAX];
    unsigned line_len, line_no;
    unsigned have_len, content_len, body_got;
} http_t;

/* Returns 0, or -1 if the request does not fit. host is sent as the Host header. */
int  http_start(http_t *h, const http_tcp_t *tcp, unsigned ip, unsigned port,
                const char *host, const char *path, http_sink_fn sink, void *user,
                unsigned now_ms);
/* Call once per frame. Returns the state. */
http_state_t http_tick(http_t *h, unsigned now_ms);
