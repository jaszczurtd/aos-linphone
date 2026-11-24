# GCC on AROS: no position-independent code.
#
# AROS relocates every executable when it loads it and has no ELF shared
# libraries. Its loader does not handle GOT relocations
# (R_X86_64_GOTPCREL, R_X86_64_REX_GOTPCRELX), which -fPIC produces and the
# final link keeps: such a program is refused with "file is not executable".
# Targets that ask for POSITION_INDEPENDENT_CODE (bctoolbox, belle-sip, belr)
# therefore get no extra flag here.
set(CMAKE_C_COMPILE_OPTIONS_PIC "")
set(CMAKE_C_COMPILE_OPTIONS_PIE "")
