#ifndef LINE_READER_H
#define LINE_READER_H

#include <stddef.h>
#include <stdint.h>

/* Transport-neutral byte -> line reassembler.
 *
 * Stream transports (TCP recv, UART ISR, GATT writes) deliver bytes in
 * arbitrary chunks: a single JSON-RPC line may span many chunks and one chunk
 * may carry several lines. This module accumulates fed bytes into a
 * caller-provided buffer, scans for the '\n' terminator, and hands each
 * complete line to a callback. It is SDK-independent and host-compilable, so
 * the chunk-absorption logic is verified without hardware.
 *
 * Framing (embedded/docs/01_SRS/SRS.md FR-WIRE): newline-delimited. '\r' is ignored,
 * empty lines are skipped, and the delivered line excludes the terminator and
 * is NUL-terminated for convenience.
 *
 * Overflow policy: a line longer than the buffer is DISCARDED and the reader
 * resynchronises -- it drops every byte up to and including the next '\n', so a
 * truncated line never leaks a corrupt prefix to the callback. */

typedef void (*line_reader_fn)(void* ctx, const char* line, size_t len);

typedef struct {
    char*          buf;      /* caller-owned accumulation buffer */
    size_t         cap;      /* capacity of buf (>= longest line + 1) */
    size_t         len;      /* current fill */
    int            overflow; /* 1 while resyncing after an oversized line */
    line_reader_fn cb;
    void*          cb_ctx;
} line_reader_t;

/* Bind `buf`/`cap` and the line callback. `cap` must be at least 2 (one byte
 * plus the NUL terminator). */
void line_reader_init(line_reader_t* lr, char* buf, size_t cap,
                      line_reader_fn cb, void* cb_ctx);

/* Feed `len` received bytes. Invokes the callback once per complete line, in
 * order, synchronously. Chunk boundaries carry no meaning. */
void line_reader_feed(line_reader_t* lr, const uint8_t* data, size_t len);

/* Drop any partially-accumulated line and clear the overflow state. Call on
 * connection loss so a new session starts on a clean framing boundary. */
void line_reader_reset(line_reader_t* lr);

#endif /* LINE_READER_H */
