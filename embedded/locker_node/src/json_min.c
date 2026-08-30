#include "json_min.h"
#include <string.h>

static const char* skip_ws(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

/* Advance past one JSON value starting at `p`, returning the position just
 * after it. Strings (and strings nested inside objects/arrays) are handled so
 * that structural characters inside strings are not miscounted. */
static const char* skip_value(const char* p, const char* end) {
    p = skip_ws(p, end);
    if (p >= end) return p;

    char c = *p;
    if (c == '"') {
        p++;
        while (p < end) {
            if (*p == '\\') { p += 2; continue; }
            if (*p == '"') return p + 1;
            p++;
        }
        return p;
    }
    if (c == '{' || c == '[') {
        int depth = 0;
        while (p < end) {
            char d = *p;
            if (d == '"') { p = skip_value(p, end); continue; }
            if (d == '{' || d == '[') depth++;
            else if (d == '}' || d == ']') { depth--; if (depth == 0) return p + 1; }
            p++;
        }
        return p;
    }
    /* number, true, false, null */
    while (p < end) {
        char d = *p;
        if (d == ',' || d == '}' || d == ']' ||
            d == ' ' || d == '\t' || d == '\n' || d == '\r') break;
        p++;
    }
    return p;
}

const char* json_member(const char* obj, size_t obj_len,
                        const char* key, size_t* out_len) {
    const char* end = obj + obj_len;
    const char* p = skip_ws(obj, end);
    if (p >= end || *p != '{') return NULL;
    p++; /* past '{' */

    size_t key_len = strlen(key);
    while (p < end) {
        p = skip_ws(p, end);
        if (p < end && *p == '}') return NULL;
        if (p >= end || *p != '"') return NULL;

        const char* k_start = p + 1;
        const char* k_end = skip_value(p, end); /* just past closing quote */
        size_t k_len = (size_t)(k_end - 1 - k_start);

        p = skip_ws(k_end, end);
        if (p >= end || *p != ':') return NULL;
        p++; /* past ':' */
        p = skip_ws(p, end);

        const char* v_start = p;
        const char* v_end = skip_value(p, end);

        if (k_len == key_len && strncmp(k_start, key, key_len) == 0) {
            *out_len = (size_t)(v_end - v_start);
            return v_start;
        }

        p = skip_ws(v_end, end);
        if (p < end && *p == ',') { p++; continue; }
        return NULL;
    }
    return NULL;
}

int json_string_eq(const char* val, size_t val_len, const char* s) {
    if (!val || val_len < 2 || val[0] != '"') return 0;
    size_t inner = val_len - 2;
    if (inner != strlen(s)) return 0;
    return strncmp(val + 1, s, inner) == 0;
}

size_t json_string_copy(const char* val, size_t val_len,
                        char* out, size_t out_size) {
    if (out_size == 0) return 0;
    if (!val || val_len < 2 || val[0] != '"') { out[0] = '\0'; return 0; }

    size_t o = 0;
    for (size_t i = 1; i + 1 < val_len && o + 1 < out_size; i++) {
        char c = val[i];
        if (c == '\\' && i + 2 < val_len) {
            char n = val[++i];
            switch (n) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '"': c = '"';  break;
                case '\\': c = '\\'; break;
                case '/': c = '/';  break;
                default: c = n;     break;
            }
        }
        out[o++] = c;
    }
    out[o] = '\0';
    return o;
}
