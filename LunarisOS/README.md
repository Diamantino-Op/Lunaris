# LunarisOS

LunarisOS is a Limine-based x86_64 OS starter that is meant to be built with Makefiles.

The tree follows the general structure of the Limine C template, but the kernel logic is written in Lua and compiled by the `Compiler` project in the parent folder.

## Layout

- `Makefile` orchestrates the ISO/HDD image build.
- `kernel/` contains the Lua kernel sources, a tiny assembly entry stub, and the kernel-specific Makefile.
- `kernel/linker-scripts/x86_64.lds` is the kernel linker script.
- `limine.conf` is the boot menu configuration.

## Build

The first build needs the Limine bootloader binaries cloned by Make, but no separate Limine protocol dependency fetch is required anymore.

```sh
make all
```

If you also want an ISO image that can be run under QEMU, install `xorriso`, `mtools`, `sgdisk`, and a Limine-compatible host toolchain.