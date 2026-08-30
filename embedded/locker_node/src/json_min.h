#ifndef JSON_MIN_H
#define JSON_MIN_H

#include <stddef.h>

/* Minimal read-only JSON scanner for the request side.
 *
 * Reference quality: it assumes well-formed, compact JSON-RPC messages. Replace
 * with a full parser as this base hardens (see README, "Roadmap"). */

/* Return a pointer to the raw value of `key` within the JSON object
 * [obj, obj + obj_len), setting *out_len to the value's length. The value is
 * returned verbatim: a string keeps its surrounding quotes, an object keeps its
 * braces, a number/literal is returned as-is. Returns NULL if absent. */
const char* json_member(const char* obj, size_t obj_len,
                        const char* key, size_t* out_len);

/* Compare a raw JSON string value (quotes included) to a C string. */
int json_string_eq(const char* val, size_t val_len, const char* s);

/* Copy the content of a raw JSON string value (unescaping a minimal set) into
 * `out` (NUL-terminated, truncated to out_size). Returns the copied length. */
size_t json_string_copy(const char* val, size_t val_len,
                        char* out, size_t out_size);

#endif /* JSON_MIN_H */
