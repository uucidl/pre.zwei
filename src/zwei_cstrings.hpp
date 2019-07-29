#ifndef ZWEI_CSTRINGS_HPP
#define ZWEI_CSTRINGS_HPP

// <CSTRINGS

#include "algos.hpp"

#include <functional> // for std::equal_to

// NOTE(nicolas): these query functions are not utf8 safe. their use is to be
// reserved to programmatic/internal strings and not used for user strings.

inline bool cstr_equals(char const *s1, char const *s2)
{
        return algos::equal_guarded(s1, s2, '\0', std::equal_to<char>());
}

inline uint64_t cstr_len(char const *s)
{
        return algos::count_guarded(s, '\0');
}

inline char const *cstr_find(char const *s, char character)
{
        return algos::find_guarded(s, '\0', character);
}

inline char const *cstr_find_last(char const *s, char character)
{
        return algos::find_last_guarded(s, '\0', character);
}

inline char const *cstr_find_prefix(char const *s, char const *prefix)
{
        auto r = algos::find_mismatch_guarded(s, prefix, '\0',
                                              std::equal_to<char>());
        if (*r.second != '\0') {
                return s + cstr_len(s); // failed
        }
        return r.first;
}

inline bool cstr_endswith(char const *s, char const *suffix)
{
        if ('\0' == *suffix) {
                return false;
        }
        return cstr_equals(cstr_find_last(s, *suffix), suffix);
}

/// try to concatenate the given c string when possible, always truncate
void cstr_cat(char *&string_last, char *string_buffer_last, char const *cstring)
{
        string_last = algos::copy_guarded_to_bounded(cstring, '\0', string_last,
                                                     string_buffer_last)
                          .second;
}

/// try to concatenate the given c string region when possible, always truncate
void cstr_cat(char *&string_last,
              char *string_buffer_last,
              char const *cstring,
              char const *cstring_end)
{
        string_last = algos::copy_to_bounded(cstring, cstring_end, string_last,
                                             string_buffer_last)
                          .second;
}

bool cstr_terminate(char *&string_last, char *buffer_last)
{
        using algos::sink;
        using algos::successor;

        if (string_last != buffer_last) {
                sink(string_last) = 0;
                string_last = successor(string_last);
                return true;
        }
        return false;
}

// ..CSTRINGS>

#endif
