#include "mcp_stream_serve.h"
#include <string.h>

/* mcp_transport read: serve the currently-staged line. mcp_server_process pulls
 * one byte at a time and reassembles it into the dispatcher's own line buffer;
 * returns 0 once the staged line is drained so the pull loop terminates. */
static int stage_read(void* ctx, uint8_t* buf, size_t max) {
    mcp_stream_serve_t* s = (mcp_stream_serve_t*)ctx;
    size_t n = s->src_n < max ? s->src_n : max;
    if (n) {
        memcpy(buf, s->src_p, n);
        s->src_p += n;
        s->src_n -= n;
    }
    return (int)n;
}

/* mcp_transport write: forward response bytes to the peer send sink. */
static int stage_write(void* ctx, const uint8_t* buf, size_t len) {
    mcp_stream_serve_t* s = (mcp_stream_serve_t*)ctx;
    if (!s->send) return (int)len;
    return s->send(s->send_ctx, (const uint8_t*)buf, len);
}

/* line_reader callback: one complete JSON-RPC line (terminator stripped). The
 * common reader has already absorbed chunk splits and bounded the length;
 * re-append '\n' and dispatch through the unmodified mcp_server. */
static void on_line(void* ctx, const char* line, size_t len) {
    mcp_stream_serve_t* s = (mcp_stream_serve_t*)ctx;
    if (len + 1 > sizeof(s->msg_buf)) return; /* bounded by line_reader cap */
    memcpy(s->msg_buf, line, len);
    s->msg_buf[len] = '\n';
    s->src_p = (const uint8_t*)s->msg_buf;
    s->src_n = len + 1;
    mcp_server_process(&s->server);
}

void mcp_stream_serve_init(mcp_stream_serve_t* s,
                           mcp_stream_send_fn send, void* send_ctx,
                           mcp_app_init_fn app_init) {
    s->send = send;
    s->send_ctx = send_ctx;
    s->src_p = NULL;
    s->src_n = 0;

    mcp_transport_t t;
    t.read = stage_read;
    t.write = stage_write;
    t.ctx = s;
    app_init(&s->server, t); /* decoupled: the target supplies its domain */

    line_reader_init(&s->reader, s->line_buf, sizeof(s->line_buf), on_line, s);
}

void mcp_stream_serve_feed(mcp_stream_serve_t* s,
                           const uint8_t* data, size_t len) {
    line_reader_feed(&s->reader, data, len);
}

void mcp_stream_serve_reset(mcp_stream_serve_t* s) {
    line_reader_reset(&s->reader);
    s->server.line_len = 0; /* also reset the dispatcher's framing */
    mcp_server_clear_subscriptions(&s->server); /* drop the gone peer's subs */
}
