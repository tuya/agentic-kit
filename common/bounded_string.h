#ifndef AGENTIC_KIT_BOUNDED_STRING_H
#define AGENTIC_KIT_BOUNDED_STRING_H

#include <stdbool.h>
#include <stddef.h>

/* Require a nonempty string terminated within capacity bytes, including its
 * NUL. Read at most capacity bytes, stopping at the first NUL. NULL, zero
 * capacity, empty and unterminated strings are invalid. */
static inline bool bounded_string_nonempty(const char *text, size_t capacity)
{
    if (!text || !capacity || !text[0]) return false;
    for (size_t i = 1; i < capacity; ++i) if (!text[i]) return true;
    return false;
}

#endif
