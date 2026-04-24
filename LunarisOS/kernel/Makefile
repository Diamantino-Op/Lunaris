.SUFFIXES:

override OUTPUT := kernel
ARCH ?= x86_64
PREFIX ?= /usr/local

LUNARIS_COMPILER ?= ../../Compiler/build/lunaris_compiler

TOOLCHAIN ?=
TOOLCHAIN_PREFIX ?=
ifneq ($(TOOLCHAIN),)
    ifeq ($(TOOLCHAIN_PREFIX),)
        TOOLCHAIN_PREFIX := $(TOOLCHAIN)-
    endif
endif

ifneq ($(TOOLCHAIN_PREFIX),)
    CC := $(TOOLCHAIN_PREFIX)gcc
    LD := $(TOOLCHAIN_PREFIX)ld
else
    CC := cc
    LD := ld
endif

ifeq ($(TOOLCHAIN),llvm)
    CC := clang
    LD := ld.lld
endif

CFLAGS := -g -O2 -pipe
CPPFLAGS :=
LDFLAGS :=

override CFLAGS += \
    -Wall \
    -Wextra \
    -nostdinc \
    -ffreestanding \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-lto \
    -fno-PIC \
    -ffunction-sections \
    -fdata-sections \
    -m64 \
    -march=x86-64 \
    -mabi=sysv \
    -mno-80387 \
    -mno-mmx \
    -mno-sse \
    -mno-sse2 \
    -mno-red-zone \
    -mcmodel=kernel

override LDFLAGS += \
    -m elf_x86_64 \
    -nostdlib \
    -static \
    -z max-page-size=0x1000 \
    --gc-sections \
    -T linker-scripts/$(ARCH).lds

override LUA_FILES := $(shell find -L src -type f -name '*.lua' | LC_ALL=C sort)
override ASM_FILES := $(shell find -L src -type f \( -name '*.S' -o -name '*.asm' \) | LC_ALL=C sort)
override LUA_SOURCES := $(filter %.lua,$(LUA_FILES))
override ASM_SOURCES_S := $(filter %.S,$(ASM_FILES))
override ASM_SOURCES_ASM := $(filter %.asm,$(ASM_FILES))
override LUA_OBJ := $(addprefix obj-$(ARCH)/,$(LUA_SOURCES:.lua=.lua.o))
override ASM_OBJ := $(addprefix obj-$(ARCH)/,$(ASM_SOURCES_S:.S=.S.o) $(ASM_SOURCES_ASM:.asm=.asm.o))
override OBJ := $(LUA_OBJ) $(ASM_OBJ)

.PHONY: all
all: bin-$(ARCH)/$(OUTPUT)

bin-$(ARCH)/$(OUTPUT): Makefile linker-scripts/$(ARCH).lds $(OBJ)
	mkdir -p "$(dir $@)"
	$(LD) $(LDFLAGS) $(OBJ) -o $@

obj-$(ARCH)/%.lua.o: %.lua Makefile $(LUNARIS_COMPILER)
	mkdir -p "$(dir $@)"
	$(LUNARIS_COMPILER) $< > $@

obj-$(ARCH)/%.S.o: %.S Makefile
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

obj-$(ARCH)/%.asm.o: %.asm Makefile
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -x assembler $< -o $@

.PHONY: clean
clean:
	rm -rf bin-$(ARCH) obj-$(ARCH)

.PHONY: distclean
distclean:
	rm -rf bin-* obj-* .cache compile_commands.json

.PHONY: install
install: all
	install -d "$(DESTDIR)$(PREFIX)/share/$(OUTPUT)"
	install -m 644 bin-$(ARCH)/$(OUTPUT) "$(DESTDIR)$(PREFIX)/share/$(OUTPUT)/$(OUTPUT)-$(ARCH)"
