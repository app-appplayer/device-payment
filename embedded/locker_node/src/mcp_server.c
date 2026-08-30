#include "mcp_server.h"
#include "json_min.h"
#include <string.h>

/* ---- transport-backed writer: streams straight to the link (FULL-friendly) ---- */
typedef struct { mcp_writer_t base; mcp_transport_t* t; } transport_writer_t;

static int tw_write(mcp_writer_t* w, const char* data, size_t len) {
    transport_writer_t* tw = (transport_writer_t*)w;
    return tw->t->write(tw->t->ctx, (const uint8_t*)data, len);
}

/* Generic writer helpers (mcp_writer_str / mcp_writer_json_string /
 * mcp_escaping_writer_init) live in the mcp_writer module. */

/* ---- JSON-RPC envelope ---- */
static void resp_head(mcp_writer_t* w, const char* id, size_t id_len) {
    mcp_writer_str(w, "{\"jsonrpc\":\"2.0\",\"id\":");
    if (id && id_len) w->write(w, id, id_len);
    else mcp_writer_str(w, "null");
    mcp_writer_str(w, ",\"result\":");
}

static void resp_tail(mcp_writer_t* w) {
    mcp_writer_str(w, "}\n");
}

static void resp_error(mcp_writer_t* w, const char* id, size_t id_len,
                       int code, const char* message) {
    mcp_writer_str(w, "{\"jsonrpc\":\"2.0\",\"id\":");
    if (id && id_len) w->write(w, id, id_len);
    else mcp_writer_str(w, "null");
    mcp_writer_str(w, ",\"error\":{\"code\":");

    char tmp[16]; int t = 0;
    int neg = code < 0;
    unsigned int v = neg ? (unsigned int)(-code) : (unsigned int)code;
    if (v == 0) tmp[t++] = '0';
    while (v) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
    char num[16]; int n = 0;
    if (neg) num[n++] = '-';
    while (t) num[n++] = tmp[--t];
    w->write(w, num, (size_t)n);

    mcp_writer_str(w, ",\"message\":");
    mcp_writer_json_string(w, message);
    mcp_writer_str(w, "}}\n");
}

/* ---- method handlers ---- */
static void do_initialize(mcp_server_t* s, mcp_writer_t* w,
                          const char* id, size_t id_len) {
    resp_head(w, id, id_len);
    mcp_writer_str(w, "{\"protocolVersion\":\"" MCP_PROTOCOL_VERSION
                      "\",\"capabilities\":{\"tools\":{},"
                      "\"resources\":{\"subscribe\":true}},"
                      "\"serverInfo\":{\"name\":");
    mcp_writer_json_string(w, s->name);
    mcp_writer_str(w, ",\"version\":");
    mcp_writer_json_string(w, s->version);
    mcp_writer_str(w, "}}");
    resp_tail(w);
}

static void do_tools_list(mcp_server_t* s, mcp_writer_t* w,
                          const char* id, size_t id_len) {
    resp_head(w, id, id_len);
    mcp_writer_str(w, "{\"tools\":[");
    for (size_t i = 0; i < s->tool_count; i++) {
        if (i) mcp_writer_str(w, ",");
        mcp_writer_str(w, "{\"name\":");
        mcp_writer_json_string(w, s->tools[i].name);
        mcp_writer_str(w, ",\"description\":");
        mcp_writer_json_string(w, s->tools[i].description ? s->tools[i].description : "");
        mcp_writer_str(w, ",\"inputSchema\":");
        mcp_writer_str(w, s->tools[i].input_schema ? s->tools[i].input_schema : "{\"type\":\"object\"}");
        mcp_writer_str(w, "}");
    }
    mcp_writer_str(w, "]}");
    resp_tail(w);
}

static void do_tools_call(mcp_server_t* s, mcp_writer_t* w,
                          const char* id, size_t id_len,
                          const char* params, size_t params_len) {
    size_t name_len = 0, args_len = 0;
    const char* name = params ? json_member(params, params_len, "name", &name_len) : NULL;
    const char* args = params ? json_member(params, params_len, "arguments", &args_len) : NULL;

    const mcp_tool_t* tool = NULL;
    if (name) {
        for (size_t i = 0; i < s->tool_count; i++) {
            if (json_string_eq(name, name_len, s->tools[i].name)) { tool = &s->tools[i]; break; }
        }
    }

    resp_head(w, id, id_len);
    if (!tool) {
        mcp_writer_str(w, "{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":\"Unknown tool\"}]}");
        resp_tail(w);
        return;
    }
    mcp_writer_str(w, "{\"isError\":false,\"content\":");
    if (tool->handler) tool->handler(args, args_len, w);
    else mcp_writer_str(w, "[]");
    mcp_writer_str(w, "}");
    resp_tail(w);
}

static void do_resources_list(mcp_server_t* s, mcp_writer_t* w,
                              const char* id, size_t id_len) {
    resp_head(w, id, id_len);
    mcp_writer_str(w, "{\"resources\":[");
    for (size_t i = 0; i < s->resource_count; i++) {
        if (i) mcp_writer_str(w, ",");
        mcp_writer_str(w, "{\"uri\":");
        mcp_writer_json_string(w, s->resources[i].uri);
        /* `name` is REQUIRED by the MCP spec; strict clients hard-fail on a
         * listing without it (AppPlayer's resources/list parse cast). The
         * uri doubles as the programmatic identifier. */
        mcp_writer_str(w, ",\"name\":");
        mcp_writer_json_string(w, s->resources[i].uri);
        mcp_writer_str(w, ",\"mimeType\":");
        mcp_writer_json_string(w, s->resources[i].mime_type ? s->resources[i].mime_type : "application/json");
        mcp_writer_str(w, "}");
    }
    mcp_writer_str(w, "]}");
    resp_tail(w);
}

static void do_resources_read(mcp_server_t* s, mcp_writer_t* w,
                              const char* id, size_t id_len,
                              const char* params, size_t params_len) {
    size_t uri_len = 0;
    const char* uri = params ? json_member(params, params_len, "uri", &uri_len) : NULL;

    const mcp_resource_t* res = NULL;
    if (uri) {
        for (size_t i = 0; i < s->resource_count; i++) {
            if (json_string_eq(uri, uri_len, s->resources[i].uri)) { res = &s->resources[i]; break; }
        }
    }
    if (!res) { resp_error(w, id, id_len, -32602, "Resource not found"); return; }

    resp_head(w, id, id_len);
    mcp_writer_str(w, "{\"contents\":[{\"uri\":");
    mcp_writer_json_string(w, res->uri);
    mcp_writer_str(w, ",\"mimeType\":");
    mcp_writer_json_string(w, res->mime_type ? res->mime_type : "application/json");
    mcp_writer_str(w, ",\"text\":\"");
    /* The body is itself JSON, delivered as a text resource: escape it as it
     * streams into the "text" string value. */
    mcp_escaping_writer_t e;
    mcp_escaping_writer_init(&e, w);
    if (res->handler) res->handler(res->uri, &e.base);
    mcp_writer_str(w, "\"}]}");
    resp_tail(w);
}

/* FR-LIVE legacy: resources/subscribe + resources/unsubscribe. `subscribe`
 * selects add vs remove. Subscriptions store the resource table's stable uri
 * pointer, so a peer can only subscribe to a resource the board serves. */
static void do_resources_subscribe(mcp_server_t* s, mcp_writer_t* w,
                                   const char* id, size_t id_len,
                                   const char* params, size_t params_len,
                                   int subscribe) {
    size_t uri_len = 0;
    const char* uri = params ? json_member(params, params_len, "uri", &uri_len) : NULL;
    if (!uri) { resp_error(w, id, id_len, -32602, "URI parameter is required"); return; }

    const char* stable = NULL;
    for (size_t i = 0; i < s->resource_count; i++) {
        if (json_string_eq(uri, uri_len, s->resources[i].uri)) { stable = s->resources[i].uri; break; }
    }
    if (!stable) { resp_error(w, id, id_len, -32602, "Resource not found"); return; }

    if (subscribe) {
        int present = 0;
        for (size_t i = 0; i < s->subscription_count; i++)
            if (s->subscriptions[i] == stable) { present = 1; break; }
        if (!present) {
            if (s->subscription_count >= MCP_SERVER_MAX_SUBSCRIPTIONS) {
                resp_error(w, id, id_len, -32603, "Too many subscriptions");
                return;
            }
            s->subscriptions[s->subscription_count++] = stable;
        }
    } else {
        for (size_t i = 0; i < s->subscription_count; i++) {
            if (s->subscriptions[i] == stable) {
                s->subscriptions[i] = s->subscriptions[--s->subscription_count];
                break;
            }
        }
    }
    resp_head(w, id, id_len);
    mcp_writer_str(w, "{}"); /* empty-result ack */
    resp_tail(w);
}

static void handle_message(mcp_server_t* s, const char* msg, size_t len) {
    size_t method_len = 0, id_len = 0, params_len = 0;
    const char* method = json_member(msg, len, "method", &method_len);
    const char* id     = json_member(msg, len, "id", &id_len);
    const char* params = json_member(msg, len, "params", &params_len);
    if (!method) return; /* response or malformed: ignore */
    if (!id) return;     /* notification: never respond, even for known methods */

    transport_writer_t tw = { { tw_write }, &s->transport };
    mcp_writer_t* w = &tw.base;

    if (json_string_eq(method, method_len, "initialize"))
        do_initialize(s, w, id, id_len);
    else if (json_string_eq(method, method_len, "tools/list"))
        do_tools_list(s, w, id, id_len);
    else if (json_string_eq(method, method_len, "tools/call"))
        do_tools_call(s, w, id, id_len, params, params_len);
    else if (json_string_eq(method, method_len, "resources/list"))
        do_resources_list(s, w, id, id_len);
    else if (json_string_eq(method, method_len, "resources/read"))
        do_resources_read(s, w, id, id_len, params, params_len);
    else if (json_string_eq(method, method_len, "resources/subscribe"))
        do_resources_subscribe(s, w, id, id_len, params, params_len, 1);
    else if (json_string_eq(method, method_len, "resources/unsubscribe"))
        do_resources_subscribe(s, w, id, id_len, params, params_len, 0);
    else
        resp_error(w, id, id_len, -32601, "Method not found");
}

void mcp_server_init(mcp_server_t* s, const char* name, const char* version,
                     const mcp_tool_t* tools, size_t tool_count,
                     const mcp_resource_t* resources, size_t resource_count,
                     mcp_transport_t transport) {
    s->name = name;
    s->version = version;
    s->tools = tools;
    s->tool_count = tool_count;
    s->resources = resources;
    s->resource_count = resource_count;
    s->transport = transport;
    s->line_len = 0;
    s->subscription_count = 0;
}

void mcp_server_process(mcp_server_t* s) {
    uint8_t b;
    while (s->transport.read(s->transport.ctx, &b, 1) > 0) {
        if (b == '\n') {
            if (s->line_len > 0) {
                s->line[s->line_len] = '\0';
                handle_message(s, s->line, s->line_len);
            }
            s->line_len = 0;
        } else if (b != '\r') {
            if (s->line_len < sizeof(s->line) - 1) {
                s->line[s->line_len++] = (char)b;
            } else {
                s->line_len = 0; /* overflow guard (reference; harden later) */
            }
        }
    }
}

/* ---- FR-LIVE: server-initiated egress ---- */

void mcp_server_emit_notification(mcp_server_t* s, const char* method,
                                  const char* params_json) {
    transport_writer_t tw = { { tw_write }, &s->transport };
    mcp_writer_t* w = &tw.base;
    mcp_writer_str(w, "{\"jsonrpc\":\"2.0\",\"method\":");
    mcp_writer_json_string(w, method);
    mcp_writer_str(w, ",\"params\":");
    mcp_writer_str(w, params_json ? params_json : "{}");
    mcp_writer_str(w, "}\n");
}

void mcp_server_notify_resource_updated(mcp_server_t* s, const char* uri) {
    int subscribed = 0;
    for (size_t i = 0; i < s->subscription_count; i++) {
        if (strcmp(s->subscriptions[i], uri) == 0) { subscribed = 1; break; }
    }
    if (!subscribed) return; /* nobody is listening for this resource */

    const mcp_resource_t* res = NULL;
    for (size_t i = 0; i < s->resource_count; i++) {
        if (strcmp(s->resources[i].uri, uri) == 0) { res = &s->resources[i]; break; }
    }

    /* notifications/resources/updated, EXTENDED form (URI + content). The
     * runtime binds the parsed content directly — self-contained, so it does not
     * depend on a host-provided resource re-reader (Standard/URI-only would need
     * one, which not every host wires). Mirrors mcp_server.notifyResourceUpdated
     * with a ResourceContent payload; the body is escaped into "text". */
    transport_writer_t tw = { { tw_write }, &s->transport };
    mcp_writer_t* w = &tw.base;
    mcp_writer_str(w, "{\"jsonrpc\":\"2.0\",\"method\":"
                      "\"notifications/resources/updated\",\"params\":{\"uri\":");
    mcp_writer_json_string(w, uri);
    if (res && res->handler) {
        mcp_writer_str(w, ",\"content\":{\"uri\":");
        mcp_writer_json_string(w, uri);
        mcp_writer_str(w, ",\"mimeType\":");
        mcp_writer_json_string(w, res->mime_type ? res->mime_type : "application/json");
        mcp_writer_str(w, ",\"text\":\"");
        mcp_escaping_writer_t e;
        mcp_escaping_writer_init(&e, w);
        res->handler(uri, &e.base); /* resource body, escaped into the string */
        mcp_writer_str(w, "\"}");
    }
    mcp_writer_str(w, "}}\n");
}

void mcp_server_clear_subscriptions(mcp_server_t* s) {
    s->subscription_count = 0;
}
