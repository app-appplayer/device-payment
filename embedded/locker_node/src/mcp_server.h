#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include <stddef.h>
#include "mcp_transport.h"
#include "mcp_writer.h"

/* MCP protocol revision advertised in `initialize`. Must match the client. */
#ifndef MCP_PROTOCOL_VERSION
#define MCP_PROTOCOL_VERSION "2025-03-26"
#endif

/* Max length of a single incoming JSON-RPC line (request). Sized per target. */
#ifndef MCP_SERVER_LINE_MAX
#define MCP_SERVER_LINE_MAX 1024
#endif

/* Max concurrent resource subscriptions per connection (FR-LIVE, legacy path).
 * A connection = one server = one session, so this bounds a single peer's live
 * subscriptions. Static (TINY-friendly): no allocation. */
#ifndef MCP_SERVER_MAX_SUBSCRIPTIONS
#define MCP_SERVER_MAX_SUBSCRIPTIONS 8
#endif

/* Tool handler: writes the MCP `content` array of a tools/call result through
 * `out`, e.g. [{"type":"text","text":"LED on"}]. `args` is the raw JSON of
 * params.arguments (or NULL). Returns 0 on success. */
typedef int (*mcp_tool_fn)(const char* args, size_t args_len, mcp_writer_t* out);

typedef struct {
    const char* name;
    const char* description;
    const char* input_schema; /* static JSON Schema text, or NULL */
    mcp_tool_fn handler;
} mcp_tool_t;

/* Resource handler: writes the RAW resource body through `out`. The server
 * wraps the body as the text of a resources/read content item, escaping it. */
typedef int (*mcp_resource_fn)(const char* uri, mcp_writer_t* out);

typedef struct {
    const char* uri;       /* e.g. "ui://app", "bundle://manifest.json" */
    const char* mime_type; /* e.g. "application/json", or NULL */
    mcp_resource_fn handler;
} mcp_resource_t;

typedef struct {
    const char* name;
    const char* version;
    const mcp_tool_t* tools;
    size_t tool_count;
    const mcp_resource_t* resources;
    size_t resource_count;
    mcp_transport_t transport;

    char line[MCP_SERVER_LINE_MAX];
    size_t line_len;

    /* FR-LIVE legacy subscriptions: URIs the peer has subscribed to. Each entry
     * points at a resource table `uri` (stable static string), so no copy. */
    const char* subscriptions[MCP_SERVER_MAX_SUBSCRIPTIONS];
    size_t subscription_count;
} mcp_server_t;

/* Domain-init hook: registers a domain's tools/resources onto the server over
 * `transport`. Lives here (not in a serve impl) so the transport-layer contract
 * can reference it without depending on any particular serve path — a target
 * passes its domain's init and any transport binds the same domain. */
typedef void (*mcp_app_init_fn)(mcp_server_t* s, mcp_transport_t transport);

/* Initialize a server with static tool/resource tables and a transport. */
void mcp_server_init(mcp_server_t* s, const char* name, const char* version,
                     const mcp_tool_t* tools, size_t tool_count,
                     const mcp_resource_t* resources, size_t resource_count,
                     mcp_transport_t transport);

/* Drain currently-available transport bytes and dispatch each complete
 * (newline-terminated) JSON-RPC message. Non-blocking: call repeatedly from
 * the board's super-loop. */
void mcp_server_process(mcp_server_t* s);

/* ---- FR-LIVE: server-initiated (server->client) egress ----
 *
 * The base is otherwise a pure request->response dispatcher; these are the ONLY
 * paths that write a frame OUTSIDE a request. Call from the same serve loop that
 * runs mcp_server_process() so writes never interleave a response mid-frame; a
 * transport served from a separate task must serialize writes itself. */

/* Emit an unsolicited JSON-RPC notification: {"jsonrpc":"2.0","method":<method>,
 * "params":<params_json>}\n. `params_json` is written verbatim (already-formed
 * JSON, or NULL for {}). The general primitive behind the helpers below. */
void mcp_server_emit_notification(mcp_server_t* s, const char* method,
                                  const char* params_json);

/* Notify subscribers that a resource changed (legacy notifications/resources/
 * updated, URI-only Standard form). No-op unless `uri` is currently subscribed.
 * A domain/producer calls this on value change (e.g. from domain_tick). */
void mcp_server_notify_resource_updated(mcp_server_t* s, const char* uri);

/* Drop all subscriptions (call on connection loss so a new peer starts clean).
 * mcp_stream_serve_reset() invokes this; direct-dispatch transports call it on
 * disconnect. */
void mcp_server_clear_subscriptions(mcp_server_t* s);

#endif /* MCP_SERVER_H */
