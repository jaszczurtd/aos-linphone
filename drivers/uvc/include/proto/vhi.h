/*
 * Calls into a VHI library (uvc<unit>.vhi of uvc.class) from MorphOS
 * programs: vhi_api() at LVO -30, arguments in D2-D6.
 *
 * Copyright (C) 2026 Marcin "Jaszczur" Kielesiński.
 * Licensed under the AROS Public License version 1.1.
 */
#ifndef PROTO_VHI_H
#define PROTO_VHI_H

#include <exec/types.h>
#include <emul/emulinterface.h>
#include <ppcinline/macros.h>

#ifndef __VHI_NOLIBBASE__
extern struct Library *VHIBase;
#endif

#define vhi_api(method, submethod, attr, errcode, handle)                                                         \
    LP5(0x1e, IPTR, vhi_api, ULONG, method, d2, ULONG, submethod, d3, APTR, attr, d4, ULONG *, errcode, d5, APTR, \
        handle, d6, , VHIBase, 0, 0, 0, 0, 0, 0)

#endif /* PROTO_VHI_H */
