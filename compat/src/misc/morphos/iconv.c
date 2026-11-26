/*
 * iconv() for MorphOS over the system iconv.library (GNU libiconv 1.11).
 *
 * The library leaves errno alone when a call fails (context_MorphOS/005),
 * and the linphone stack depends on it: bctoolbox grows its output buffer
 * only on E2BIG. These functions call the library and set errno
 * themselves:
 *
 *  - iconv_open(): EINVAL when the library refuses the pair;
 *  - iconv(): E2BIG when the next character converts into a larger scratch
 *    buffer, EINVAL when the input ends inside a UTF-8 sequence, EILSEQ
 *    otherwise (an invalid sequence, or a character the target lacks).
 *
 * The scratch conversion advances the state of stateful encodings, which
 * the stack does not use (it converts between UTF-8 and 8-bit sets). The
 * library does not know the name "UTF8", which is mapped to "UTF-8".
 */
#include <iconv.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <proto/iconv.h>

struct aos_iconv {
    libiconv_t cd;
    int from_utf8;
};

/* "UTF8" with an optional "//..." suffix becomes "UTF-8..." in buf. */
static const char *aos_iconv_name(const char *name, char *buf, size_t size) {
    if (strncasecmp(name, "UTF8", 4) == 0 && (name[4] == '\0' || name[4] == '/')) {
        size_t rest = strlen(name + 4);
        if (5 + rest < size) {
            memcpy(buf, "UTF-8", 5);
            memcpy(buf + 5, name + 4, rest + 1);
            return buf;
        }
    }
    return name;
}

static int aos_iconv_is_utf8(const char *name) {
    return strncasecmp(name, "UTF-8", 5) == 0 && (name[5] == '\0' || name[5] == '/');
}

iconv_t aos_iconv_open(const char *tocode, const char *fromcode) {
    char to_buf[64], from_buf[64];
    if (tocode == NULL || fromcode == NULL) {
        errno = EINVAL;
        return (iconv_t)-1;
    }
    const char *to = aos_iconv_name(tocode, to_buf, sizeof(to_buf));
    const char *from = aos_iconv_name(fromcode, from_buf, sizeof(from_buf));
    struct aos_iconv *h = malloc(sizeof(*h));
    if (h == NULL) {
        errno = ENOMEM;
        return (iconv_t)-1;
    }
    h->cd = libiconv_open(to, from);
    if (h->cd == (libiconv_t)-1) {
        free(h);
        errno = EINVAL;
        return (iconv_t)-1;
    }
    h->from_utf8 = aos_iconv_is_utf8(from);
    return (iconv_t)h;
}

int aos_iconv_close(iconv_t cd) {
    struct aos_iconv *h = (struct aos_iconv *)cd;
    if (h == NULL || cd == (iconv_t)-1) {
        errno = EBADF;
        return -1;
    }
    libiconv_close(h->cd);
    free(h);
    return 0;
}

/* Length of the UTF-8 sequence at p (up to len bytes): its length when it
   is complete and valid, 0 when the input ends inside a valid prefix, -1
   when it is invalid. */
static int aos_utf8_sequence(const unsigned char *p, size_t len) {
    unsigned char c = p[0];
    int need;
    unsigned char lo = 0x80, hi = 0xbf; /* range of the second byte */
    if (c < 0x80) {
        return 1;
    } else if (c >= 0xc2 && c <= 0xdf) {
        need = 2;
    } else if (c >= 0xe0 && c <= 0xef) {
        need = 3;
        if (c == 0xe0) {
            lo = 0xa0; /* no overlong forms */
        } else if (c == 0xed) {
            hi = 0x9f; /* no surrogates */
        }
    } else if (c >= 0xf0 && c <= 0xf4) {
        need = 4;
        if (c == 0xf0) {
            lo = 0x90;
        } else if (c == 0xf4) {
            hi = 0x8f; /* nothing above U+10FFFF */
        }
    } else {
        return -1;
    }
    for (int i = 1; i < need; ++i) {
        if ((size_t)i >= len) {
            return 0;
        }
        unsigned char min = i == 1 ? lo : 0x80, max = i == 1 ? hi : 0xbf;
        if (p[i] < min || p[i] > max) {
            return -1;
        }
    }
    return need;
}

/* errno for a failed conversion, judged from where it stopped. */
static int aos_iconv_errno(struct aos_iconv *h, char **inbuf, size_t *inleft) {
    if (inbuf == NULL || *inbuf == NULL || inleft == NULL || *inleft == 0) {
        return E2BIG; /* a reset call whose shift sequence did not fit */
    }
    if (h->from_utf8) {
        int n = aos_utf8_sequence((const unsigned char *)*inbuf, *inleft);
        if (n == 0) {
            return EINVAL;
        }
        if (n < 0) {
            return EILSEQ;
        }
    }
    /* Does the next character convert when there is room for it? */
    char scratch[32];
    const char *in = *inbuf;
    size_t in_len = *inleft < 16 ? *inleft : 16;
    char *out = scratch;
    size_t out_len = sizeof(scratch);
    libiconv(h->cd, &in, &in_len, &out, &out_len);
    if (in != *inbuf || out != scratch) {
        return E2BIG;
    }
    return EILSEQ;
}

size_t aos_iconv(iconv_t cd, char **inbuf, size_t *inleft, char **outbuf, size_t *outleft) {
    struct aos_iconv *h = (struct aos_iconv *)cd;
    if (h == NULL || cd == (iconv_t)-1) {
        errno = EBADF;
        return (size_t)-1;
    }
    size_t rc = libiconv(h->cd, (const char **)inbuf, inleft, outbuf, outleft);
    if (rc == (size_t)-1) {
        errno = aos_iconv_errno(h, inbuf, inleft);
    }
    return rc;
}
