#include "json_lexical.h"

#include <string.h>

static bool unicode_escape(const char *data, size_t len, size_t *offset, unsigned *code)
{
    unsigned value = 0;
    if (len - *offset < 4) return false;
    for (unsigned i = 0; i < 4; ++i) {
        unsigned char c = (unsigned char)data[(*offset)++];
        int digit = c >= '0' && c <= '9' ? c - '0' :
                    c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                    c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (digit < 0) return false;
        value = (value << 4) | (unsigned)digit;
    }
    *code = value;
    return true;
}

/* The lead byte restricts the first continuation byte as well as the length:
 * exclude overlong encodings, UTF-16 surrogates and scalars above U+10FFFF. */
static bool utf8_tail(const char *data, size_t len, size_t *offset, unsigned char lead)
{
    unsigned extra;
    unsigned char lo = 0x80, hi = 0xbf;
    if (lead >= 0xc2 && lead <= 0xdf) extra = 1;
    else if (lead >= 0xe0 && lead <= 0xef) {
        extra = 2;
        if (lead == 0xe0) lo = 0xa0;
        if (lead == 0xed) hi = 0x9f;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
        extra = 3;
        if (lead == 0xf0) lo = 0x90;
        if (lead == 0xf4) hi = 0x8f;
    } else return false;
    if (len - *offset < extra || (unsigned char)data[*offset] < lo ||
        (unsigned char)data[*offset] > hi) return false;
    ++*offset;
    while (--extra > 0) {
        unsigned char c = (unsigned char)data[(*offset)++];
        if (c < 0x80 || c > 0xbf) return false;
    }
    return true;
}

bool json_lexical_scan_string(const char *data, size_t len, size_t *offset)
{
    if (!data || !offset || *offset >= len || data[*offset] != '"') return false;
    size_t i = *offset + 1;
    while (i < len) {
        unsigned char c = (unsigned char)data[i++];
        if (c == '"') { *offset = i; return true; }
        if (c < 0x20) return false;
        if (c == '\\') {
            unsigned code, low;
            if (i == len) return false;
            c = (unsigned char)data[i++];
            if (c == 'u') {
                if (!unicode_escape(data, len, &i, &code) || !code ||
                    (code >= 0xdc00 && code <= 0xdfff)) return false;
                if (code >= 0xd800 && code <= 0xdbff) {
                    if (len - i < 6 || data[i] != '\\' || data[i + 1] != 'u') return false;
                    i += 2;
                    if (!unicode_escape(data, len, &i, &low) || low < 0xdc00 || low > 0xdfff)
                        return false;
                }
            } else if (c != '"' && c != '\\' && c != '/' && c != 'b' &&
                       c != 'f' && c != 'n' && c != 'r' && c != 't') return false;
        } else if (c >= 0x80 && !utf8_tail(data, len, &i, c)) return false;
    }
    return false;
}

static bool delimiter(char c)
{
    return c == ',' || c == ']' || c == '}' || c == ' ' ||
           c == '\t' || c == '\n' || c == '\r';
}

bool json_lexical_scan_number(const char *data, size_t len, size_t *offset)
{
    if (!data || !offset || *offset >= len) return false;
    size_t i = *offset;
    if (data[i] == '-') ++i;
    if (i == len) return false;
    if (data[i] == '0') ++i;
    else {
        if (data[i] < '1' || data[i] > '9') return false;
        do { ++i; } while (i < len && data[i] >= '0' && data[i] <= '9');
    }
    if (i < len && data[i] == '.') {
        ++i;
        if (i == len || data[i] < '0' || data[i] > '9') return false;
        do { ++i; } while (i < len && data[i] >= '0' && data[i] <= '9');
    }
    if (i < len && (data[i] == 'e' || data[i] == 'E')) {
        ++i;
        if (i < len && (data[i] == '+' || data[i] == '-')) ++i;
        if (i == len || data[i] < '0' || data[i] > '9') return false;
        do { ++i; } while (i < len && data[i] >= '0' && data[i] <= '9');
    }
    if (i < len && !delimiter(data[i])) return false;
    *offset = i;
    return true;
}

bool json_lexical_validate(const char *data, size_t len)
{
    if (!data || !len) return false;
    size_t i = 0;
    unsigned depth = 0;
    while (i < len) {
        unsigned char c = (unsigned char)data[i];
        if (c == '"') { if (!json_lexical_scan_string(data, len, &i)) return false; }
        else if (c == '-' || (c >= '0' && c <= '9')) {
            if (!json_lexical_scan_number(data, len, &i)) return false;
        } else if (c == '{' || c == '[') { if (++depth > JSON_LEXICAL_MAX_DEPTH) return false; ++i; }
        else if (c == '}' || c == ']') { if (!depth) return false; --depth; ++i; }
        else if (c == ':' || c == ',' || c == ' ' || c == '\t' || c == '\n' || c == '\r') ++i;
        else {
            size_t word_len;
            const char *word;
            if (c == 't') { word = "true"; word_len = 4; }
            else if (c == 'f') { word = "false"; word_len = 5; }
            else if (c == 'n') { word = "null"; word_len = 4; }
            else return false;
            if (len - i < word_len || memcmp(data + i, word, word_len)) return false;
            i += word_len;
            if (i < len && !delimiter(data[i])) return false;
        }
    }
    return depth == 0;
}
