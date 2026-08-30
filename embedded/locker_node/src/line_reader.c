#include "line_reader.h"

void line_reader_init(line_reader_t* lr, char* buf, size_t cap,
                      line_reader_fn cb, void* cb_ctx) {
    lr->buf = buf;
    lr->cap = cap;
    lr->len = 0;
    lr->overflow = 0;
    lr->cb = cb;
    lr->cb_ctx = cb_ctx;
}

void line_reader_reset(line_reader_t* lr) {
    lr->len = 0;
    lr->overflow = 0;
}

void line_reader_feed(line_reader_t* lr, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c == '\n') {
            if (lr->overflow) {
                /* End of the discarded oversized line: resync complete. */
                lr->overflow = 0;
                lr->len = 0;
            } else if (lr->len > 0) {
                lr->buf[lr->len] = '\0';
                if (lr->cb) lr->cb(lr->cb_ctx, lr->buf, lr->len);
                lr->len = 0;
            }
            /* empty line (len == 0): skipped */
        } else if (c == '\r') {
            /* framing: ignore carriage returns */
        } else if (lr->overflow) {
            /* still discarding the oversized line, wait for next '\n' */
        } else if (lr->len + 1 < lr->cap) {
            lr->buf[lr->len++] = c;
        } else {
            /* line exceeds buffer: discard it and resync to the next '\n' */
            lr->overflow = 1;
            lr->len = 0;
        }
    }
}
