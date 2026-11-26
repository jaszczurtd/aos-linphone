/*
 * nl_langinfo(CODESET) for MorphOS from the character set of the user's
 * locale. locale.library gives it as an IANA MIBenum (loc_CodeSet, 0 for
 * the default ISO-8859-1); the table maps the sets MorphOS locales use to
 * the names iconv.library knows.
 */
#include <langinfo.h>

#include <proto/exec.h>
#include <proto/locale.h>

struct aos_codeset {
    ULONG mib;
    const char *name;
};

static const struct aos_codeset aos_codesets[] = {
    {3, "US-ASCII"},
    {4, "ISO-8859-1"},
    {5, "ISO-8859-2"},
    {6, "ISO-8859-3"},
    {7, "ISO-8859-4"},
    {8, "ISO-8859-5"},
    {9, "ISO-8859-6"},
    {10, "ISO-8859-7"},
    {11, "ISO-8859-8"},
    {12, "ISO-8859-9"},
    {13, "ISO-8859-10"},
    {106, "UTF-8"},
    {109, "ISO-8859-13"},
    {110, "ISO-8859-14"},
    {111, "ISO-8859-15"},
    {112, "ISO-8859-16"},
    {2084, "KOI8-R"},
    {2088, "KOI8-U"},
    {2250, "WINDOWS-1250"},
    {2251, "WINDOWS-1251"},
    {2252, "WINDOWS-1252"},
    {2253, "WINDOWS-1253"},
    {2254, "WINDOWS-1254"},
    {2255, "WINDOWS-1255"},
    {2256, "WINDOWS-1256"},
    {2257, "WINDOWS-1257"},
    {2258, "WINDOWS-1258"},
};

char *nl_langinfo(nl_item item) {
    static char empty[] = "";
    if (item != CODESET) {
        return empty;
    }
    ULONG mib = 0;
    struct Locale *locale = OpenLocale(NULL);
    if (locale != NULL) {
        mib = locale->loc_CodeSet;
        CloseLocale(locale);
    }
    for (size_t i = 0; i < sizeof(aos_codesets) / sizeof(aos_codesets[0]); ++i) {
        if (aos_codesets[i].mib == mib) {
            return (char *)aos_codesets[i].name;
        }
    }
    return (char *)"ISO-8859-1";
}
