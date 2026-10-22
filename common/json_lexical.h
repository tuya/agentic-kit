/* Internal, allocation-free lexical checks before cJSON normalization.
 * This is not a JSON parser: callers still validate document structure with
 * cJSON, and apply their own domain constraints. Strings must be UTF-8 and may
 * not decode to NUL, because cJSON exposes NUL-terminated C strings. */
#ifndef AGENTIC_KIT_JSON_LEXICAL_H
#define AGENTIC_KIT_JSON_LEXICAL_H

#include <stdbool.h>
#include <stddef.h>

#define JSON_LEXICAL_MAX_DEPTH 32u

/* Scan one token at *offset without reading outside data[0..len). Advance the
 * offset only on success; failure includes a missing or malformed token. */
bool json_lexical_scan_string(const char *data, size_t len, size_t *offset);
bool json_lexical_scan_number(const char *data, size_t len, size_t *offset);

/* Check all lexical forms and cap nesting before invoking cJSON's recursive
 * parser. This accepts punctuation sequences that only cJSON can reject. */
bool json_lexical_validate(const char *data, size_t len);

#endif
