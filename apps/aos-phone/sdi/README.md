# SDI headers

`SDI/SDI_hook.h` (1.24) and `SDI/SDI_compiler.h` (1.35) are the SDI headers
of the sditools project (<http://www.sf.net/projects/sditools/>), public
domain, taken unchanged from the AROS source tree. They hide the
differences between AROS and MorphOS in hook functions and BOOPSI
dispatchers (`HOOKPROTO`, `SDISPATCHER`, `ENTRY`). The MorphOS SDK does not
ship them.
