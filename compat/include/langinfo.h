/*
 * langinfo.h - nl_langinfo() for MorphOS, implemented by libaoscompat;
 * libnix has neither the header nor the function. On other systems this
 * header passes on to the C library's <langinfo.h>.
 *
 * Only CODESET is answered: the IANA name of the character set of the
 * user's locale (locale.library), in the form iconv.library accepts.
 */
#ifndef __MORPHOS__
#include_next <langinfo.h>
#else

#ifndef AOS_LANGINFO_H
#define AOS_LANGINFO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int nl_item;

#define CODESET 0

/**
 * @brief Describes the current locale.
 *
 * @param item  CODESET; any other item gives an empty string.
 * @return For CODESET the character set name, such as "ISO-8859-2" or
 *         "UTF-8" ("ISO-8859-1" when the locale names none). The string
 *         stays valid for the whole program.
 */
char *nl_langinfo(nl_item item);

#ifdef __cplusplus
}
#endif

#endif /* AOS_LANGINFO_H */

#endif /* __MORPHOS__ */
