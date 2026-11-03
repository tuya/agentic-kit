#include "json_lexical.h"

#include <stdio.h>
#include <string.h>

static unsigned passed, failed, total, cases;
#define CHECK(expr) do { if (!(expr)) { \
    printf("%s:%d: %s\n", __FILE__, __LINE__, #expr); return -1; \
} } while (0)
#define RUN(fn) do { ++total; if (fn() == 0) ++passed; else ++failed; } while (0)

static int test_string_scalars_and_escapes(void)
{
    const struct { const char *text; bool valid; } inputs[] = {
        {"\"\"", true}, {"\"ascii\"", true},
        {"\"\xc2\x80\"", true}, {"\"\xdf\xbf\"", true},
        {"\"\xe0\xa0\x80\"", true}, {"\"\xed\x9f\xbf\"", true},
        {"\"\xee\x80\x80\"", true}, {"\"\xef\xbf\xbf\"", true},
        {"\"\xf0\x90\x80\x80\"", true}, {"\"\xf4\x8f\xbf\xbf\"", true},
        {"\"\\u0001\\u007f\\u00A2\\u4e2d\"", true},
        {"\"\\ud800\\udc00\\uDBFF\\uDFFF\"", true},
        {"\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"", true},
        {"\"literal \\\\u0000\"", true},
        {"\"\x80\"", false}, {"\"\xbf\"", false},
        {"\"\xc0\x80\"", false}, {"\"\xc1\xbf\"", false},
        {"\"\xe0\x9f\xbf\"", false}, {"\"\xf0\x8f\xbf\xbf\"", false},
        {"\"\xed\xa0\x80\"", false}, {"\"\xed\xbf\xbf\"", false},
        {"\"\xf4\x90\x80\x80\"", false}, {"\"\xf5\x80\x80\x80\"", false},
        {"\"\xc2x\"", false}, {"\"\xe1\x80x\"", false},
        {"\"\xf1\x80\x80x\"", false}, {"\"\xff\"", false},
        {"\"\xc2", false}, {"\"\xe0\xa0", false}, {"\"\xf0\x90\x80", false},
        {"\"\\u0000\"", false}, {"\"\\ud800\"", false}, {"\"\\udc00\"", false},
        {"\"\\ud800\\u0041\"", false}, {"\"\\ud800\\ud800\"", false},
        {"\"\\ud800x\"", false}, {"\"\\u00x0\"", false}, {"\"\\u123\"", false},
        {"\"\\x20\"", false}, {"\"\\v\"", false}, {"\"\\", false},
        {"\"\n\"", false}, {"\"unterminated", false},
    };
    for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        size_t offset = 0;
        ++cases;
        CHECK(json_lexical_scan_string(inputs[i].text, strlen(inputs[i].text), &offset) == inputs[i].valid);
        CHECK(offset == (inputs[i].valid ? strlen(inputs[i].text) : 0));
        CHECK(json_lexical_validate(inputs[i].text, strlen(inputs[i].text)) == inputs[i].valid);
    }
    return 0;
}

static int test_number_grammar(void)
{
    const struct { const char *text; bool valid; } inputs[] = {
        {"0", true}, {"-0", true}, {"123", true}, {"-123", true},
        {"0.125", true}, {"-0.125e+2", true}, {"1E-3", true}, {"1e0", true},
        {"01", false}, {"-01", false}, {"+1", false}, {".1", false},
        {"1.", false}, {"1e", false}, {"1e+", false}, {"1e-", false},
        {"--1", false}, {"1x", false}, {"1\v", false}, {"NaN", false},
    };
    for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        size_t offset = 0;
        ++cases;
        CHECK(json_lexical_scan_number(inputs[i].text, strlen(inputs[i].text), &offset) == inputs[i].valid);
        CHECK(offset == (inputs[i].valid ? strlen(inputs[i].text) : 0));
        CHECK(json_lexical_validate(inputs[i].text, strlen(inputs[i].text)) == inputs[i].valid);
    }
    return 0;
}

static int test_complete_bounded_slice(void)
{
    const char string[] = {'x', '"', 'o', 'k', '"', 'x'};
    const char number[] = {'x', '-', '2', '.', '5', ',', 'x'};
    const char raw_nul[] = {'"', 'a', '\0', 'b', '"'};
    const char nul_tail[] = {'{', '}', '\0', 'x'};
    size_t offset = 1;
    ++cases; CHECK(json_lexical_scan_string(string, 5, &offset) && offset == 5);
    offset = 1;
    ++cases; CHECK(!json_lexical_scan_string(string, 4, &offset) && offset == 1);
    offset = 1;
    ++cases; CHECK(json_lexical_scan_number(number, 6, &offset) && offset == 5);
    offset = 1;
    ++cases; CHECK(!json_lexical_scan_number(number, 4, &offset) && offset == 1);
    ++cases; CHECK(!json_lexical_validate(raw_nul, sizeof(raw_nul)));
    ++cases; CHECK(!json_lexical_validate(nul_tail, sizeof(nul_tail)));
    ++cases; CHECK(json_lexical_validate(nul_tail, 2));
    ++cases; CHECK(!json_lexical_validate(NULL, 1));
    ++cases; CHECK(!json_lexical_validate("", 0));
    offset = sizeof(string);
    ++cases; CHECK(!json_lexical_scan_string(string, sizeof(string), &offset));
    ++cases; CHECK(!json_lexical_scan_number(number, sizeof(number), NULL));
    return 0;
}

static int test_depth_and_literal_boundaries(void)
{
    char nested[JSON_LEXICAL_MAX_DEPTH * 2 + 3];
    size_t n = JSON_LEXICAL_MAX_DEPTH;
    memset(nested, '[', n); nested[n] = '0'; memset(nested + n + 1, ']', n);
    ++cases; CHECK(json_lexical_validate(nested, n * 2 + 1));
    memmove(nested + 1, nested, n * 2 + 1); nested[0] = '['; nested[n * 2 + 2] = ']';
    ++cases; CHECK(!json_lexical_validate(nested, sizeof(nested)));
    ++cases; CHECK(!json_lexical_validate("{", 1));
    ++cases; CHECK(!json_lexical_validate("}", 1));
    ++cases; CHECK(!json_lexical_validate("[truefalse]", 11));
    ++cases; CHECK(!json_lexical_validate("[null0]", 7));
    ++cases; CHECK(json_lexical_validate("[true,false,null]", 17));
    return 0;
}

int main(void)
{
    RUN(test_string_scalars_and_escapes);
    RUN(test_number_grammar);
    RUN(test_complete_bounded_slice);
    RUN(test_depth_and_literal_boundaries);
    printf("=== JSON lexical: %u passed, %u failed (%u total, %u cases) ===\n",
           passed, failed, total, cases);
    return failed ? 1 : 0;
}
