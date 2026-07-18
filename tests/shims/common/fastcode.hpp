#pragma once
// Host-build stub for common/fastcode.hpp. On the RP2040, FASTCODE places a
// function in a custom .fastcode linker section that gets copied to RAM at
// startup (QSPI flash fetch is slow at 176 MHz). That section attribute
// syntax isn't valid on host object formats (Mach-O/ELF vary, and we don't
// need RAM placement on a host binary anyway) — make it a no-op here.
#define FASTCODE
