/*
 * Time functions libnix lacks: the POSIX timezone variable (as a function,
 * see aos/compat_morphos.h) and strptime().
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

long aos_timezone(void);
char *strptime(const char *s, const char *format, struct tm *tm);

/* Seconds west of UTC of standard time, as mktime() of libnix applies it:
   the same broken-down time read as local time and as UTC. */
long aos_timezone(void) {
    time_t now = time(NULL);
    struct tm utc;
    if (gmtime_r(&now, &utc) == NULL) {
        return 0;
    }
    struct tm local = utc;
    local.tm_isdst = 0;
    time_t as_local = mktime(&local);
    time_t as_utc = timegm(&utc);
    if (as_local == (time_t)-1 || as_utc == (time_t)-1) {
        return 0;
    }
    return (long)(as_local - as_utc);
}

static const char *const aos_days[7] = {"sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"};
static const char *const aos_months[12] = {"january", "february", "march", "april", "may", "june",
                                           "july", "august", "september", "october", "november", "december"};

/* Reads a decimal number of at most max_digits digits within [lo, hi]. */
static const char *aos_number(const char *s, int max_digits, int lo, int hi, int *out) {
    int value = 0, digits = 0, negative = 0;
    if (*s == '-' && lo < 0) {
        negative = 1;
        ++s;
    } else if (*s == '+') {
        ++s;
    }
    while (digits < max_digits && isdigit((unsigned char)*s)) {
        value = value * 10 + (*s++ - '0');
        ++digits;
    }
    if (digits == 0) {
        return NULL;
    }
    if (negative) {
        value = -value;
    }
    if (value < lo || value > hi) {
        return NULL;
    }
    *out = value;
    return s;
}

/* Matches a full or three-letter English name from names; returns the
   index through *out. */
static const char *aos_name(const char *s, const char *const *names, int count, int *out) {
    for (int i = 0; i < count; ++i) {
        size_t full = strlen(names[i]);
        if (strncasecmp(s, names[i], full) == 0) {
            *out = i;
            return s + full;
        }
    }
    for (int i = 0; i < count; ++i) {
        if (strncasecmp(s, names[i], 3) == 0) {
            *out = i;
            return s + 3;
        }
    }
    return NULL;
}

static const char *aos_strptime(const char *s, const char *format, struct tm *tm) {
    int value, pm = -1, hour12 = -1;
    for (const char *f = format; *f != '\0' && s != NULL; ++f) {
        if (isspace((unsigned char)*f)) {
            while (isspace((unsigned char)*s)) {
                ++s;
            }
            continue;
        }
        if (*f != '%') {
            if (*s++ != *f) {
                return NULL;
            }
            continue;
        }
        ++f;
        if (*f == 'E' || *f == 'O') {
            ++f; /* alternative representations: same as the plain ones here */
        }
        switch (*f) {
        case '%':
            s = *s == '%' ? s + 1 : NULL;
            break;
        case 'n':
        case 't':
            while (isspace((unsigned char)*s)) {
                ++s;
            }
            break;
        case 'Y':
            if ((s = aos_number(s, 4, 0, 9999, &value)) != NULL) {
                tm->tm_year = value - 1900;
            }
            break;
        case 'y':
            if ((s = aos_number(s, 2, 0, 99, &value)) != NULL) {
                tm->tm_year = value < 69 ? value + 100 : value;
            }
            break;
        case 'C':
            if ((s = aos_number(s, 2, 0, 99, &value)) != NULL) {
                tm->tm_year = value * 100 - 1900 + tm->tm_year % 100;
            }
            break;
        case 'm':
            if ((s = aos_number(s, 2, 1, 12, &value)) != NULL) {
                tm->tm_mon = value - 1;
            }
            break;
        case 'd':
        case 'e':
            while (*f == 'e' && *s == ' ') {
                ++s;
            }
            if ((s = aos_number(s, 2, 1, 31, &value)) != NULL) {
                tm->tm_mday = value;
            }
            break;
        case 'j':
            if ((s = aos_number(s, 3, 1, 366, &value)) != NULL) {
                tm->tm_yday = value - 1;
            }
            break;
        case 'H':
        case 'k':
            if ((s = aos_number(s, 2, 0, 23, &value)) != NULL) {
                tm->tm_hour = value;
            }
            break;
        case 'I':
        case 'l':
            if ((s = aos_number(s, 2, 1, 12, &value)) != NULL) {
                hour12 = value;
            }
            break;
        case 'M':
            if ((s = aos_number(s, 2, 0, 59, &value)) != NULL) {
                tm->tm_min = value;
            }
            break;
        case 'S':
            if ((s = aos_number(s, 2, 0, 60, &value)) != NULL) {
                tm->tm_sec = value;
            }
            break;
        case 'p':
            if (strncasecmp(s, "AM", 2) == 0) {
                pm = 0;
                s += 2;
            } else if (strncasecmp(s, "PM", 2) == 0) {
                pm = 1;
                s += 2;
            } else {
                s = NULL;
            }
            break;
        case 'a':
        case 'A':
            if ((s = aos_name(s, aos_days, 7, &value)) != NULL) {
                tm->tm_wday = value;
            }
            break;
        case 'b':
        case 'B':
        case 'h':
            if ((s = aos_name(s, aos_months, 12, &value)) != NULL) {
                tm->tm_mon = value;
            }
            break;
        case 'w':
            if ((s = aos_number(s, 1, 0, 6, &value)) != NULL) {
                tm->tm_wday = value;
            }
            break;
        case 'D':
            s = aos_strptime(s, "%m/%d/%y", tm);
            break;
        case 'F':
            s = aos_strptime(s, "%Y-%m-%d", tm);
            break;
        case 'R':
            s = aos_strptime(s, "%H:%M", tm);
            break;
        case 'T':
            s = aos_strptime(s, "%H:%M:%S", tm);
            break;
        case 'c':
            s = aos_strptime(s, "%a %b %e %H:%M:%S %Y", tm);
            break;
        case 'x':
            s = aos_strptime(s, "%m/%d/%y", tm);
            break;
        case 'X':
            s = aos_strptime(s, "%H:%M:%S", tm);
            break;
        case 'z': {
            /* Z, +hh, +hhmm or +hh:mm */
            if (*s == 'Z') {
                ++s;
                break;
            }
            int sign = *s == '-' ? -1 : (*s == '+' ? 1 : 0);
            if (sign == 0 || !isdigit((unsigned char)s[1]) || !isdigit((unsigned char)s[2])) {
                s = NULL;
                break;
            }
            long offset = ((s[1] - '0') * 10 + (s[2] - '0')) * 3600L;
            s += 3;
            if (*s == ':') {
                ++s;
            }
            if (isdigit((unsigned char)s[0]) && isdigit((unsigned char)s[1])) {
                offset += ((s[0] - '0') * 10 + (s[1] - '0')) * 60L;
                s += 2;
            }
            tm->tm_gmtoff = sign * offset;
            break;
        }
        case 'Z':
            /* A zone name: skip letters. */
            while (isalpha((unsigned char)*s)) {
                ++s;
            }
            break;
        default:
            return NULL;
        }
    }
    if (s == NULL) {
        return NULL;
    }
    if (hour12 >= 0) {
        tm->tm_hour = hour12 % 12 + (pm == 1 ? 12 : 0);
    } else if (pm == 1 && tm->tm_hour < 12) {
        tm->tm_hour += 12;
    }
    return s;
}

/* strptime() for the C locale. Fields the format does not mention keep
   their values, except tm_isdst, tm_gmtoff and tm_zone, which are reset
   (0, 0, NULL); %z sets tm_gmtoff. */
char *strptime(const char *s, const char *format, struct tm *tm) {
    tm->tm_isdst = 0;
    tm->tm_gmtoff = 0;
    tm->tm_zone = NULL;
    return (char *)aos_strptime(s, format, tm);
}
