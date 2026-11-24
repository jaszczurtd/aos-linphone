#include "text.h"

#include <errno.h>
#include <iconv.h>
#include <langinfo.h>
#include <stdio.h>
#include <string.h>

#define NO_CONVERTER ((iconv_t)(-1))

static iconv_t from_utf8 = NO_CONVERTER;
static iconv_t to_utf8 = NO_CONVERTER;

int aos_text_init(void) {
    /* "unknown" from the AROS C library names the system code page. */
    const char *system = nl_langinfo(CODESET);
    char target[64];
    snprintf(target, sizeof(target), "%s//TRANSLIT", system);
    from_utf8 = iconv_open(target, "UTF-8");
    to_utf8 = iconv_open("UTF-8//TRANSLIT", system);
    return from_utf8 != NO_CONVERTER && to_utf8 != NO_CONVERTER ? 0 : -1;
}

void aos_text_cleanup(void) {
    if (from_utf8 != NO_CONVERTER) {
        iconv_close(from_utf8);
        from_utf8 = NO_CONVERTER;
    }
    if (to_utf8 != NO_CONVERTER) {
        iconv_close(to_utf8);
        to_utf8 = NO_CONVERTER;
    }
}

static void convert(iconv_t cd, const char *in, char *out, size_t out_size) {
    if (out_size == 0) {
        return;
    }
    char *dst = out;
    size_t room = out_size - 1;
    if (cd == NO_CONVERTER) {
        for (; *in != '\0' && room > 0; ++in, --room) {
            *dst++ = ((unsigned char)*in < 0x80) ? *in : '?';
        }
        *dst = '\0';
        return;
    }

    iconv(cd, NULL, NULL, NULL, NULL);
    char *src = (char *)in;
    size_t left = strlen(in);
    while (left > 0) {
        if (iconv(cd, &src, &left, &dst, &room) != (size_t)-1) {
            break;
        }
        /* E2BIG: the output is full; iconv stops before a character that
           does not fit, so the text ends at a character boundary. */
        if ((errno != EILSEQ && errno != EINVAL) || room == 0) {
            break;
        }
        /* An invalid or cut sequence: mark it and go on after its first
           byte. */
        *dst++ = '?';
        --room;
        ++src;
        --left;
    }
    *dst = '\0';
}

void aos_text_from_utf8(const char *in, char *out, size_t out_size) {
    convert(from_utf8, in, out, out_size);
}

void aos_text_to_utf8(const char *in, char *out, size_t out_size) {
    convert(to_utf8, in, out, out_size);
}
