/*
 * iconv.h - character set conversion for AROS, implemented by libaoscompat.
 *
 * Converts between UTF-8, US-ASCII, ISO-8859-1 and any 8-bit character set
 * known to codesets.library. The name "unknown", which nl_langinfo(CODESET)
 * of posixc returns, and an empty name both mean the system code page
 * (the default codeset of codesets.library). Other multi-byte encodings
 * (UTF-16, UTF-32, UCS-2...) are not supported. A "//TRANSLIT" suffix on
 * the target replaces characters it cannot represent with '?'; "//IGNORE"
 * drops them and skips invalid input bytes.
 */
#ifndef AOS_ICONV_H
#define AOS_ICONV_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *iconv_t;

/**
 * @brief Opens a conversion from @p fromcode to @p tocode.
 *
 * Names are case-insensitive. UTF-8, US-ASCII and ISO-8859-1 are built in;
 * other names are looked up in codesets.library.
 *
 * @return Conversion descriptor, or (iconv_t)-1 with errno EINVAL when a
 *         character set is unknown, ENOMEM when out of memory.
 */
iconv_t iconv_open(const char *tocode, const char *fromcode);

/**
 * @brief Converts as many characters as fit, advancing both buffers.
 *
 * @param cd            Descriptor from iconv_open().
 * @param inbuf         Input pointer, advanced past converted bytes; a NULL
 *                      @p inbuf or *@p inbuf resets the (stateless)
 *                      conversion and returns 0.
 * @param inbytesleft   Input bytes left, decreased accordingly.
 * @param outbuf        Output pointer, advanced past written bytes.
 * @param outbytesleft  Output space left, decreased accordingly.
 * @return Number of characters replaced or dropped, or (size_t)-1 with errno
 *         E2BIG (output full), EILSEQ (invalid or unrepresentable input),
 *         EINVAL (incomplete UTF-8 sequence at the end of the input) or
 *         EBADF (bad descriptor).
 */
size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft,
             char **outbuf, size_t *outbytesleft);

/**
 * @brief Releases a descriptor from iconv_open().
 * @return 0, or -1 with errno EBADF.
 */
int iconv_close(iconv_t cd);

#ifdef __cplusplus
}
#endif

#endif /* AOS_ICONV_H */
