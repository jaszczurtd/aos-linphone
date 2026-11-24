/*
 * text - conversion between the UTF-8 of SIP messages and the 8-bit code
 * page of the system, which Zune displays and the keyboard produces.
 *
 * Characters the code page lacks become '?'. Both functions always end the
 * output with a NUL and cut it at a character boundary when it does not fit.
 */
#ifndef AOS_PHONE_TEXT_H
#define AOS_PHONE_TEXT_H

#include <stddef.h>

/* Opens the converters. Returns 0, or -1 when the system code page is
   unknown to the converter; the functions then copy only ASCII. */
int aos_text_init(void);

void aos_text_cleanup(void);

void aos_text_from_utf8(const char *in, char *out, size_t out_size);

void aos_text_to_utf8(const char *in, char *out, size_t out_size);

#endif
