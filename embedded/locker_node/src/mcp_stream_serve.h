#ifndef MCP_STREAM_SERVE_H
#define MCP_STREAM_SERVE_H

#include <stddef.h>
#include <stdint.h>

/* base + util on the include path */
#include "mcp_server.h"
#include "line_reader.h"

/* Byte-sink for outbound response bytes (writes to the peer link). Returns the
 * count written, or negative on error. */
typedef int (*mcp_stream_send_fn)(void* ctx, const uint8_t* data, size_t len);

/* mcp_app_init_fn (the domain-init hook) is declared in mcp_server.h so the
 * transport-layer contract can reference it without depending on this serve
 * impl. */

/* Newline-stream MCP serving session -- the §2.5 assembly point.
 *
 * Wires three layers without modifying any of them:
 *   - util `line_reader` (byte -> line reassembly, arbitrary chunk splits)
 *   - core `mcp_server` (unmodified JSON-RPC dispatch)
 *   - a domain (tools/resources) via the injected mcp_app_init_fn
 * The transport is abstracted to a single send callback, so the same code path
 * backs both the host_test (capturing sink) and a real assembly (TCP peer /
 * UART / GATT). The port only supplies received bytes (mcp_stream_serve_feed)
 * and a send sink. Transport- and domain-neutral, host-compilable. */
typedef struct {
    mcp_server_t   server;
    line_reader_t  reader;
    char           line_buf[MCP_SERVER_LINE_MAX];     /* reassembly buffer */
    char           msg_buf[MCP_SERVER_LINE_MAX + 1];  /* staged line + '\n' */
    const uint8_t* src_p;    /* staged-line read cursor for the dispatcher */
    size_t         src_n;
    mcp_stream_send_fn send;
    void*          send_ctx;
} mcp_stream_serve_t;

/* Initialize the session: outbound byte sink + the domain to serve. */
void mcp_stream_serve_init(mcp_stream_serve_t* s,
                           mcp_stream_send_fn send, void* send_ctx,
                           mcp_app_init_fn app_init);

/* Feed received bytes. Each complete line is dispatched through mcp_server and
 * its response is written to the send sink, synchronously. */
void mcp_stream_serve_feed(mcp_stream_serve_t* s,
                           const uint8_t* data, size_t len);

/* Reset framing on connection loss (drop partial line + dispatcher buffer). */
void mcp_stream_serve_reset(mcp_stream_serve_t* s);

#endif /* MCP_STREAM_SERVE_H */
