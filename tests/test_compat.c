/*
 * libaoscompat: iconv() as bctoolbox's charconv uses it (system code page
 * from nl_langinfo(CODESET) to UTF-8 and back), the dlopen() and
 * backtrace() stubs, and setpriority().
 */
#include "aos_test.h"

#include <dlfcn.h>
#include <errno.h>
#include <execinfo.h>
#include <iconv.h>
#include <langinfo.h>
#include <locale.h>
#include <string.h>
#include <sys/resource.h>

/* Converts the whole of in; returns the output length or -1. */
static long convert(const char *to, const char *from, const char *in, size_t in_len,
                    char *out, size_t out_size)
{
    iconv_t cd = iconv_open(to, from);
    if (cd == (iconv_t)-1) {
        return -1;
    }
    char *ip = (char *)in, *op = out;
    size_t il = in_len, ol = out_size;
    size_t rc = iconv(cd, &ip, &il, &op, &ol);
    iconv_close(cd);
    return rc == (size_t)-1 ? -1 : (long)(out_size - ol);
}

static void check_iconv(void)
{
    char out[64];
    /* "zażółć" in UTF-8 and in ISO-8859-2 */
    static const char utf8[] = "za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87";
    static const char latin2[] = "za\xbf\xf3\xb3\xe6";

    long n = convert("UTF-8", "ISO-8859-1", "caf\xe9", 4, out, sizeof(out));
    AOS_CHECK(n == 5 && memcmp(out, "caf\xc3\xa9", 5) == 0, "ISO-8859-1 to UTF-8 (built in)");

    n = convert("ISO-8859-2", "UTF-8", utf8, strlen(utf8), out, sizeof(out));
    AOS_CHECK(n == 6 && memcmp(out, latin2, 6) == 0, "UTF-8 to ISO-8859-2 through codesets.library");
    n = convert("utf8", "iso-8859-2", latin2, 6, out, sizeof(out));
    AOS_CHECK(n == (long)strlen(utf8) && memcmp(out, utf8, (size_t)n) == 0,
              "ISO-8859-2 to UTF-8, names in another spelling");

    errno = 0;
    AOS_CHECK(iconv_open("NO-SUCH-CHARSET", "UTF-8") == (iconv_t)-1 && errno == EINVAL,
              "an unknown character set fails with EINVAL");

    /* Output full: convert what fits, keep the rest. */
    iconv_t cd = iconv_open("UTF-8", "ISO-8859-2");
    char *ip = (char *)latin2, *op = out;
    size_t il = 6, ol = 4;
    errno = 0;
    size_t rc = iconv(cd, &ip, &il, &op, &ol);
    AOS_CHECK(rc == (size_t)-1 && errno == E2BIG && il == 3 && ol == 0 && op == out + 4,
              "E2BIG after converting what fits");
    ol = sizeof(out) - 4;
    rc = iconv(cd, &ip, &il, &op, &ol);
    AOS_CHECK(rc == 0 && il == 0 && op - out == (long)strlen(utf8) && memcmp(out, utf8, strlen(utf8)) == 0,
              "a second call finishes the conversion");
    AOS_CHECK(iconv(cd, NULL, NULL, NULL, NULL) == 0, "a reset call returns 0");

    /* Room for "za" and one byte of the two-byte "ż". */
    ip = (char *)latin2;
    il = 6;
    op = out;
    ol = 3;
    errno = 0;
    rc = iconv(cd, &ip, &il, &op, &ol);
    AOS_CHECK(rc == (size_t)-1 && errno == E2BIG && il == 4 && ol == 1 && op == out + 2,
              "E2BIG does not split a multi-byte character");
    iconv_close(cd);

    errno = 0;
    AOS_CHECK(iconv_open("UTF-16", "UTF-8") == (iconv_t)-1 && errno == EINVAL,
              "UTF-16 is refused instead of converted as an 8-bit table");

    static const char bad[] = "a\xc3(";
    errno = 0;
    AOS_CHECK(convert("ISO-8859-1", "UTF-8", bad, 3, out, sizeof(out)) == -1 && errno == EILSEQ,
              "invalid UTF-8 fails with EILSEQ");
    errno = 0;
    AOS_CHECK(convert("ISO-8859-1", "UTF-8", "a\xc3", 2, out, sizeof(out)) == -1 && errno == EINVAL,
              "a truncated UTF-8 sequence fails with EINVAL");
    errno = 0;
    AOS_CHECK(convert("ISO-8859-1", "UTF-8", "a\xc0", 2, out, sizeof(out)) == -1 && errno == EILSEQ,
              "a truncated overlong lead byte fails with EILSEQ, not EINVAL");
    errno = 0;
    AOS_CHECK(convert("UTF-8", "UTF-8", "a\xed\xa0", 3, out, sizeof(out)) == -1 && errno == EILSEQ,
              "a truncated surrogate fails with EILSEQ");
    AOS_CHECK(convert("ISO-8859-1//IGNORE", "UTF-8", "a\xff" "b", 3, out, sizeof(out)) == 2 &&
              memcmp(out, "ab", 2) == 0, "//IGNORE skips an invalid input byte");
    errno = 0;
    AOS_CHECK(convert("ISO-8859-1", "UTF-8", "\xe2\x82\xac", 3, out, sizeof(out)) == -1 &&
              errno == EILSEQ, "a character the target lacks fails with EILSEQ");
    AOS_CHECK(convert("ISO-8859-1//TRANSLIT", "UTF-8", "\xe2\x82\xac!", 4, out, sizeof(out)) == 2 &&
              memcmp(out, "?!", 2) == 0, "//TRANSLIT replaces it with '?'");

    /* bctoolbox convert_from_to("locale", "UTF-8") */
    setlocale(LC_CTYPE, "");
    const char *codeset = nl_langinfo(CODESET);
    printf("info: nl_langinfo(CODESET) = \"%s\"\n", codeset ? codeset : "(null)");
    cd = codeset != NULL ? iconv_open("UTF-8", codeset) : (iconv_t)-1;
    AOS_CHECK(cd != (iconv_t)-1, "iconv_open() from the system code page to UTF-8");
    if (cd != (iconv_t)-1) {
        iconv_close(cd);
    }
    n = codeset != NULL ? convert(codeset, "UTF-8", "plain ascii", 12, out, sizeof(out)) : -1;
    AOS_CHECK(n == 12 && strcmp(out, "plain ascii") == 0, "ASCII text survives UTF-8 to the system code page");
}

static void check_stubs(void)
{
    AOS_CHECK(dlerror() == NULL, "dlerror() is NULL before any failure");
    AOS_CHECK(dlopen(NULL, RTLD_LAZY) == NULL, "dlopen() fails");
    const char *msg = dlerror();
    AOS_CHECK(msg != NULL && msg[0] != '\0', "dlerror() then describes the failure");
    AOS_CHECK(dlerror() == NULL, "and is cleared by reading it");
    AOS_CHECK(dlsym(NULL, "main") == NULL && dlerror() != NULL, "dlsym() fails");

    void *frames[8];
    AOS_CHECK(backtrace(frames, 8) == 0, "backtrace() reports no frames");
    AOS_CHECK(backtrace_symbols(frames, 0) == NULL, "backtrace_symbols() returns NULL");

    errno = 0;
    AOS_CHECK(setpriority(PRIO_PROCESS, 0, -19) == -1 && errno == EPERM, "setpriority() refuses with EPERM");
    AOS_CHECK(getpriority(PRIO_PROCESS, 0) == 0, "getpriority() reports 0");
}

int main(void)
{
    check_iconv();
    check_stubs();
    return aos_test_result();
}
