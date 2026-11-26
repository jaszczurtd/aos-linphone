/*
 * Differences between the AROS (Zune) and MorphOS (MUI) headers that the
 * aos-phone sources meet.
 */
#ifndef AOS_PHONE_MUI_COMPAT_H
#define AOS_PHONE_MUI_COMPAT_H

#include <libraries/mui.h>
#include <utility/hooks.h>

/* Superclass name for MUI_CreateCustomClass(): a ClassID (CONST_STRPTR) on
   AROS, a char * on MorphOS. */
#ifdef __MORPHOS__
#define AOS_SUPERCLASS(name) ((char *)(name))
#else
#define AOS_SUPERCLASS(name) ((ClassID)(name))
#endif

/* A function in h_Entry or h_SubEntry of a hook: through APTR, since its
   prototype is not that of HOOKFUNC. */
#define AOS_HOOKFUNC(func) ((HOOKFUNC)(APTR)(func))

#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif

#endif /* AOS_PHONE_MUI_COMPAT_H */
