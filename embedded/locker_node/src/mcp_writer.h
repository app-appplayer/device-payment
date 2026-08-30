#ifndef MCP_WRITER_H
#define MCP_WRITER_H

#include <stddef.h>

/* Capability-neutral output sink.
 *
 * Handlers stream their output through a writer instead of filling a fixed
 * buffer, so the same handler backs a TINY target (the writer wraps a small
 * static buffer) and a FULL target (the writer streams straight to the
 * transport). The contract imposes no allocation strategy. */
typedef struct mcp_writer {
    /* Append `len` bytes. Returns bytes written, or negative on error. */
    int (*write)(struct mcp_writer* self, const char* data, size_t len);
} mcp_writer_t;

/* Write a NUL-terminated C string verbatim. */
int mcp_writer_str(mcp_writer_t* w, const char* s);

/* Write `s` as a quoted, JSON-escaped string (surrounding quotes included). */
int mcp_writer_json_string(mcp_writer_t* w, const char* s);

/* Writer that JSON-string-escapes everything written to it and forwards to an
 * inner writer — used to embed a raw body as a JSON string value. After
 * mcp_escaping_writer_init(), write to &e.base. */
typedef struct {
    mcp_writer_t base;
    mcp_writer_t* inner;
} mcp_escaping_writer_t;

void mcp_escaping_writer_init(mcp_escaping_writer_t* e, mcp_writer_t* inner);

#endif /* MCP_WRITER_H */
