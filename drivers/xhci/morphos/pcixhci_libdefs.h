/*
 * What genmodule's libdefs header gives the AROS pcixhci.device: module
 * name, version and base type. MorphOS port.
 *
 * Copyright (C) 2026 Marcin "Jaszczur" Kielesiński.
 * Licensed under the AROS Public License version 1.1, like the driver.
 */
#ifndef PCIXHCI_LIBDEFS_H
#define PCIXHCI_LIBDEFS_H

#define MOD_NAME_STRING "pcixhci.device"
#define VERSION_NUMBER  1
#define REVISION_NUMBER 60
#define VERSION_STRING  "1.60"
#define DATE_STRING     "26.11.2026"
#define LIBBASETYPE     struct PCIDevice
#define LIBBASETYPEPTR  struct PCIDevice *

#endif /* PCIXHCI_LIBDEFS_H */
