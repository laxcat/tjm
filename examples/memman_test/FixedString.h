#pragma once

struct FixedString {
    char * ptr;
    size_t len;

    FixedString(char * ptr = nullptr, size_t len = 0) :
    ptr(ptr),
    len(len)
    {}
};
