/*
 * iconv() over codesets.library.
 *
 * Every character passes through its Unicode code point: the source is
 * decoded one character at a time and the code point encoded into the
 * target. 8-bit character sets use the 256-entry table of their
 * codesets.library codeset; the reverse table is built at iconv_open().
 */
#include <iconv.h>

#include <aros/symbolsets.h>
#include <errno.h>
#include <exec/types.h>
#include <libraries/codesets.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* The library base is private to this file, so that an application that
   opens codesets.library itself keeps its own CodesetsBase. */
#define CodesetsBase aos_iconv_codesets_base
static struct Library *CodesetsBase;
#include <proto/codesets.h>
#include <proto/exec.h>

#define AOS_ICONV_MAGIC 0x69636f6eUL /* "icon" */
#define AOS_ICONV_NAME_MAX 64

enum aos_charset_kind {
    AOS_CS_UTF8,
    AOS_CS_ASCII,
    AOS_CS_LATIN1,
    AOS_CS_TABLE,
};

struct aos_ucs_code {
    ULONG ucs;
    UBYTE code;
};

struct aos_charset {
    enum aos_charset_kind kind;
    ULONG to_ucs[256];                 /* AOS_CS_TABLE: byte -> code point */
    struct aos_ucs_code from_ucs[256]; /* AOS_CS_TABLE: sorted by code point */
    int from_count;
};

struct aos_iconv {
    ULONG magic;
    int translit;
    int ignore;
    struct aos_charset from;
    struct aos_charset to;
};

static pthread_once_t aos_iconv_once = PTHREAD_ONCE_INIT;

static void aos_iconv_open_library(void)
{
    CodesetsBase = OpenLibrary((CONST_STRPTR)CODESETSNAME, CODESETSVER);
}

static void aos_iconv_close_library(void)
{
    if (CodesetsBase != NULL) {
        CloseLibrary(CodesetsBase);
        CodesetsBase = NULL;
    }
}
ADD2EXIT(aos_iconv_close_library, 20);

/* Case-insensitive comparison that ignores '-' and '_'. */
static int aos_iconv_name_is(const char *name, const char *want)
{
    for (;;) {
        while (*name == '-' || *name == '_') {
            ++name;
        }
        while (*want == '-' || *want == '_') {
            ++want;
        }
        char a = *name, b = *want;
        if (a >= 'a' && a <= 'z') {
            a = (char)(a - 'a' + 'A');
        }
        if (b >= 'a' && b <= 'z') {
            b = (char)(b - 'a' + 'A');
        }
        if (a != b) {
            return 0;
        }
        if (a == '\0') {
            return 1;
        }
        ++name;
        ++want;
    }
}

static int aos_ucs_cmp(const void *a, const void *b)
{
    ULONG x = ((const struct aos_ucs_code *)a)->ucs;
    ULONG y = ((const struct aos_ucs_code *)b)->ucs;
    return x < y ? -1 : x > y;
}

/* Fills cs for the character set name; returns 0 or an errno value. */
static int aos_charset_init(struct aos_charset *cs, const char *name)
{
    static const char *const utf8[] = {"UTF-8", NULL};
    static const char *const ascii[] = {"US-ASCII", "ASCII", "ANSI_X3.4-1968", "646", NULL};
    static const char *const latin1[] = {"ISO-8859-1", "ISO8859-1", "LATIN1", "L1", NULL};
    static const struct {
        const char *const *names;
        enum aos_charset_kind kind;
    } builtin[] = {
        {utf8, AOS_CS_UTF8},
        {ascii, AOS_CS_ASCII},
        {latin1, AOS_CS_LATIN1},
    };

    memset(cs, 0, sizeof(*cs));
    for (size_t i = 0; i < sizeof(builtin) / sizeof(builtin[0]); ++i) {
        for (const char *const *n = builtin[i].names; *n != NULL; ++n) {
            if (aos_iconv_name_is(name, *n)) {
                cs->kind = builtin[i].kind;
                return 0;
            }
        }
    }

    /* Multi-byte encodings other than UTF-8 are not supported; codesets'
       8-bit tables would convert them wrongly instead of failing. */
    if (strncasecmp(name, "UTF", 3) == 0 || strncasecmp(name, "UCS", 3) == 0) {
        return EINVAL;
    }

    pthread_once(&aos_iconv_once, aos_iconv_open_library);
    if (CodesetsBase == NULL) {
        return EINVAL;
    }
    /* nl_langinfo(CODESET) of posixc answers "unknown" for the system
       code page, so that name (and an empty one) selects the default
       codeset of codesets.library, which follows the system locale. */
    int system_codeset = name[0] == '\0' || aos_iconv_name_is(name, "unknown");
    struct TagItem tags[] = {
        {CSA_FallbackToDefault, system_codeset ? TRUE : FALSE},
        {TAG_DONE, 0},
    };
    struct codeset *codeset =
        CodesetsFindA(system_codeset ? NULL : (CONST_STRPTR)name, tags);
    if (codeset == NULL) {
        return EINVAL;
    }
    if (codeset->name != NULL && aos_iconv_name_is(codeset->name, "UTF-8")) {
        cs->kind = AOS_CS_UTF8;
        return 0;
    }

    cs->kind = AOS_CS_TABLE;
    for (int b = 0; b < 256; ++b) {
        ULONG ucs = codeset->table[b].ucs4;
        cs->to_ucs[b] = ucs;
        /* Bytes 1..255 with code point 0 stay unmapped; byte 0 is NUL.
           codesets.library gives undefined bytes of a codeset loaded from
           a file their own value, so those pass through as Latin-1. */
        if (ucs != 0 || b == 0) {
            cs->from_ucs[cs->from_count].ucs = ucs;
            cs->from_ucs[cs->from_count].code = (UBYTE)b;
            ++cs->from_count;
        }
    }
    qsort(cs->from_ucs, (size_t)cs->from_count, sizeof(cs->from_ucs[0]), aos_ucs_cmp);
    return 0;
}

iconv_t iconv_open(const char *tocode, const char *fromcode)
{
    if (tocode == NULL || fromcode == NULL) {
        errno = EINVAL;
        return (iconv_t)-1;
    }

    /* Split "NAME//TRANSLIT" or "NAME//IGNORE" on the target. */
    char to_name[AOS_ICONV_NAME_MAX];
    size_t len = strcspn(tocode, "/");
    if (len >= sizeof(to_name) || strlen(fromcode) >= AOS_ICONV_NAME_MAX) {
        errno = EINVAL;
        return (iconv_t)-1;
    }
    memcpy(to_name, tocode, len);
    to_name[len] = '\0';

    struct aos_iconv *cd = calloc(1, sizeof(*cd));
    if (cd == NULL) {
        errno = ENOMEM;
        return (iconv_t)-1;
    }
    for (const char *opt = tocode + len; *opt != '\0'; ++opt) {
        if (strncasecmp(opt, "//TRANSLIT", 10) == 0) {
            cd->translit = 1;
        } else if (strncasecmp(opt, "//IGNORE", 8) == 0) {
            cd->ignore = 1;
        }
    }

    int err = aos_charset_init(&cd->from, fromcode);
    if (err == 0) {
        err = aos_charset_init(&cd->to, to_name);
    }
    if (err != 0) {
        free(cd);
        errno = err;
        return (iconv_t)-1;
    }
    cd->magic = AOS_ICONV_MAGIC;
    return (iconv_t)cd;
}

int iconv_close(iconv_t handle)
{
    struct aos_iconv *cd = (struct aos_iconv *)handle;
    if (cd == NULL || cd == (struct aos_iconv *)-1 || cd->magic != AOS_ICONV_MAGIC) {
        errno = EBADF;
        return -1;
    }
    cd->magic = 0;
    free(cd);
    return 0;
}

/* Decodes one character. Returns the bytes used, 0 for an incomplete UTF-8
   sequence, or -1 for invalid input. */
static int aos_decode(const struct aos_charset *cs, const UBYTE *in, size_t left, ULONG *ucs)
{
    UBYTE c = in[0];
    switch (cs->kind) {
    case AOS_CS_ASCII:
        if (c >= 0x80) {
            return -1;
        }
        *ucs = c;
        return 1;
    case AOS_CS_LATIN1:
        *ucs = c;
        return 1;
    case AOS_CS_TABLE:
        if (c != 0 && cs->to_ucs[c] == 0) {
            return -1;
        }
        *ucs = cs->to_ucs[c];
        return 1;
    case AOS_CS_UTF8:
        break;
    }

    /* Well-formed sequences as in table 3-7 of the Unicode standard. Each
       byte is checked as soon as it is available, so an invalid prefix at
       the end of the input is EILSEQ, not an incomplete sequence. */
    if (c < 0x80) {
        *ucs = c;
        return 1;
    }
    int n;
    if (c >= 0xc2 && c <= 0xdf) {
        n = 2;
        *ucs = c & 0x1f;
    } else if (c >= 0xe0 && c <= 0xef) {
        n = 3;
        *ucs = c & 0x0f;
    } else if (c >= 0xf0 && c <= 0xf4) {
        n = 4;
        *ucs = c & 0x07;
    } else {
        return -1; /* continuation byte, overlong 0xc0/0xc1, or above U+10FFFF */
    }
    for (int i = 1; i < n; ++i) {
        if ((size_t)i >= left) {
            return 0;
        }
        UBYTE lo = 0x80, hi = 0xbf;
        if (i == 1) {
            if (c == 0xe0) {
                lo = 0xa0; /* overlong */
            } else if (c == 0xed) {
                hi = 0x9f; /* surrogates */
            } else if (c == 0xf0) {
                lo = 0x90; /* overlong */
            } else if (c == 0xf4) {
                hi = 0x8f; /* above U+10FFFF */
            }
        }
        if (in[i] < lo || in[i] > hi) {
            return -1;
        }
        *ucs = (*ucs << 6) | (in[i] & 0x3f);
    }
    return n;
}

/* Encodes one code point into buf (at least 4 bytes). Returns the length,
   or -1 when the target cannot represent it. */
static int aos_encode(const struct aos_charset *cs, ULONG ucs, UBYTE *buf)
{
    switch (cs->kind) {
    case AOS_CS_ASCII:
        if (ucs >= 0x80) {
            return -1;
        }
        buf[0] = (UBYTE)ucs;
        return 1;
    case AOS_CS_LATIN1:
        if (ucs >= 0x100) {
            return -1;
        }
        buf[0] = (UBYTE)ucs;
        return 1;
    case AOS_CS_TABLE: {
        struct aos_ucs_code key = {ucs, 0};
        const struct aos_ucs_code *hit = bsearch(&key, cs->from_ucs, (size_t)cs->from_count,
                                                 sizeof(key), aos_ucs_cmp);
        if (hit == NULL) {
            return -1;
        }
        buf[0] = hit->code;
        return 1;
    }
    case AOS_CS_UTF8:
        break;
    }

    if (ucs < 0x80) {
        buf[0] = (UBYTE)ucs;
        return 1;
    }
    if (ucs < 0x800) {
        buf[0] = (UBYTE)(0xc0 | (ucs >> 6));
        buf[1] = (UBYTE)(0x80 | (ucs & 0x3f));
        return 2;
    }
    if (ucs < 0x10000) {
        buf[0] = (UBYTE)(0xe0 | (ucs >> 12));
        buf[1] = (UBYTE)(0x80 | ((ucs >> 6) & 0x3f));
        buf[2] = (UBYTE)(0x80 | (ucs & 0x3f));
        return 3;
    }
    buf[0] = (UBYTE)(0xf0 | (ucs >> 18));
    buf[1] = (UBYTE)(0x80 | ((ucs >> 12) & 0x3f));
    buf[2] = (UBYTE)(0x80 | ((ucs >> 6) & 0x3f));
    buf[3] = (UBYTE)(0x80 | (ucs & 0x3f));
    return 4;
}

size_t iconv(iconv_t handle, char **inbuf, size_t *inbytesleft,
             char **outbuf, size_t *outbytesleft)
{
    struct aos_iconv *cd = (struct aos_iconv *)handle;
    if (cd == NULL || cd == (struct aos_iconv *)-1 || cd->magic != AOS_ICONV_MAGIC) {
        errno = EBADF;
        return (size_t)-1;
    }
    /* Stateless conversions: a reset has nothing to flush. */
    if (inbuf == NULL || *inbuf == NULL) {
        return 0;
    }
    if (inbytesleft == NULL || outbuf == NULL || *outbuf == NULL || outbytesleft == NULL) {
        errno = EINVAL;
        return (size_t)-1;
    }

    size_t replaced = 0;
    while (*inbytesleft > 0) {
        ULONG ucs;
        int used = aos_decode(&cd->from, (const UBYTE *)*inbuf, *inbytesleft, &ucs);
        if (used == 0) {
            errno = EINVAL;
            return (size_t)-1;
        }
        if (used < 0) {
            if (cd->ignore) {
                /* //IGNORE skips invalid input bytes as well. */
                ++*inbuf;
                --*inbytesleft;
                ++replaced;
                continue;
            }
            errno = EILSEQ;
            return (size_t)-1;
        }

        UBYTE buf[4];
        int len = aos_encode(&cd->to, ucs, buf);
        int substituted = 0;
        if (len < 0) {
            if (cd->translit) {
                buf[0] = '?';
                len = 1;
            } else if (cd->ignore) {
                len = 0;
            } else {
                errno = EILSEQ;
                return (size_t)-1;
            }
            substituted = 1;
        }
        if ((size_t)len > *outbytesleft) {
            /* The character stays in the input for the next call. */
            errno = E2BIG;
            return (size_t)-1;
        }
        memcpy(*outbuf, buf, (size_t)len);
        *outbuf += len;
        *outbytesleft -= (size_t)len;
        *inbuf += used;
        *inbytesleft -= (size_t)used;
        replaced += (size_t)substituted;
    }
    return replaced;
}
