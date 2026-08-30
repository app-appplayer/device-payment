#include "mcp_writer.h"
#include <string.h>

int mcp_writer_str(mcp_writer_t* w, const char* s) {
    return w->write(w, s, strlen(s));
}

static int esc_write(mcp_writer_t* w, const char* data, size_t len) {
    static const char hex[] = "0123456789abcdef";
    mcp_escaping_writer_t* e = (mcp_escaping_writer_t*)w;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];
        switch (c) {
            case '"':  e->inner->write(e->inner, "\\\"", 2); break;
            case '\\': e->inner->write(e->inner, "\\\\", 2); break;
            case '\n': e->inner->write(e->inner, "\\n", 2); break;
            case '\r': e->inner->write(e->inner, "\\r", 2); break;
            case '\t': e->inner->write(e->inner, "\\t", 2); break;
            default:
                if (c < 0x20) {
                    char u[6] = { '\\', 'u', '0', '0', hex[(c >> 4) & 0xf], hex[c & 0xf] };
                    e->inner->write(e->inner, u, 6);
                } else {
                    char ch = (char)c;
                    e->inner->write(e->inner, &ch, 1);
                }
        }
    }
    return (int)len;
}

void mcp_escaping_writer_init(mcp_escaping_writer_t* e, mcp_writer_t* inner) {
    e->base.write = esc_write;
    e->inner = inner;
}

int mcp_writer_json_string(mcp_writer_t* w, const char* s) {
    mcp_escaping_writer_t e;
    mcp_escaping_writer_init(&e, w);
    w->write(w, "\"", 1);
    e.base.write(&e.base, s, strlen(s));
    w->write(w, "\"", 1);
    return 0;
}
