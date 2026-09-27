# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Copyright (C) mrc-sk and imjumping
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU Affero General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version. See LICENSE for the full license text and the additional
# non-commercial restriction terms that apply to this software.
# BerryOS Makefile (M0): x86_64 self-written bootloader + minimal kernel.
#
# Requirements: clang (LLVM), ld.lld, llvm-objcopy, python3, qemu-system-x86_64
#   -> install once with:  python tools/setup_toolchain.py
#
# Targets:
#   make            build disk.img
#   make run        build and launch in QEMU (serial -> stdio, VGA -> window)
#   make clean      remove build/

TARGET   := x86_64-none-elf
# Tools: override on command line if not on PATH, e.g.
#   make CC="C:/Program Files/LLVM/bin/clang.exe" LD="C:/Program Files/LLVM/bin/ld.lld.exe"
# NOTE: use := not ?= — CC/LD/AS are built-in GNU make variables with default
# values (cc/ld/as), so ?= would never override them.
CC       := clang
AS       := clang
LD       := ld.lld
OBJCOPY  := llvm-objcopy
PY       := python

KERNEL_DIR := src/kernel
BOOT_DIR   := src/boot/x86_64
INC_DIR    := $(KERNEL_DIR)/include
BUILD      := build

K_CFLAGS   := -target $(TARGET) -ffreestanding -mno-red-zone -mcmodel=kernel \
              -nostdlib -nostdinc -Wall -Wextra -O2 -I$(INC_DIR) \
              -mno-sse -mno-sse2 -mno-avx -mno-mmx
K_ASFLAGS  := -target $(TARGET) -c
B_ASFLAGS  := -target $(TARGET) -c

K_OBJS := $(BUILD)/main.o \
          $(BUILD)/mm/pmm.o \
          $(BUILD)/mm/slab.o \
          $(BUILD)/mm/paging.o \
          $(BUILD)/mm/elf.o \
          $(BUILD)/lib/kstring.o \
          $(BUILD)/sched/sched.o \
          $(BUILD)/syscall/syscall.o \
          $(BUILD)/user/user_main.o \
          $(BUILD)/user/user_elf.o \
          $(BUILD)/arch/x86_64/serial.o \
          $(BUILD)/arch/x86_64/vga.o \
          $(BUILD)/arch/x86_64/gdt.o \
          $(BUILD)/arch/x86_64/idt.o \
          $(BUILD)/arch/x86_64/isr.o \
          $(BUILD)/drivers/keyboard.o \
          $(BUILD)/drivers/mouse.o \
          $(BUILD)/drivers/bui.o \
          $(BUILD)/drivers/bppg.o \
          $(BUILD)/drivers/tcon.o \
          $(BUILD)/drivers/desktop.o \
          $(BUILD)/drivers/ata.o \
          $(BUILD)/drivers/fb.o \
          $(BUILD)/drivers/dev.o \
          $(BUILD)/fs/berryfs.o \
          $(BUILD)/drivers/fbcon.o \
          $(BUILD)/drivers/splash.o \
          $(BUILD)/drivers/splash_logo.o \
          $(BUILD)/drivers/font8x8.o \
          $(BUILD)/arch/x86_64/pic.o \
          $(BUILD)/arch/x86_64/timer.o

K_ASM_OBJ  := $(BUILD)/start.o \
              $(BUILD)/interrupt.o \
              $(BUILD)/switch.o
KERNEL_ELF := $(BUILD)/kernel.elf
KERNEL_BIN := $(BUILD)/kernel.bin

BOOT_OBJ  := $(BUILD)/boot.o
BOOT_ELF  := $(BUILD)/boot.elf
BOOT_BIN  := $(BUILD)/boot.bin

STAGE2_OBJ := $(BUILD)/stage2.o
STAGE2_ELF := $(BUILD)/stage2.elf
STAGE2_BIN := $(BUILD)/stage2.bin

MBR_OBJ  := $(BUILD)/mbr.o
MBR_ELF  := $(BUILD)/mbr.elf
MBR_BIN  := $(BUILD)/mbr.bin

DISK := $(BUILD)/disk.img

# Kernel image size -> bootloader transfer length.  Recursive (`=`, not `:=`)
# so the tools/kern_size.py probe runs at recipe time, once kernel.bin exists.
# Without this the loader would use its 256 KiB fallback default.
KERNEL_SECTORS = $(shell $(PY) tools/kern_size.py $(KERNEL_BIN))
KERNEL_COPYB   = $(shell $(PY) tools/kern_size.py $(KERNEL_BIN) --bytes)

# ---- cross-platform helpers ----
# GnuWin32 make on Windows runs recipes via cmd.exe, so `mkdir -p` / `rm -rf`
# are invalid there. Pick cmd-compatible variants on Windows.
ifeq ($(OS),Windows_NT)
MKDIR_P = @if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
RM_RF  = @if exist $(BUILD) rd /s /q $(BUILD)
else
MKDIR_P = @mkdir -p $(dir $@)
RM_RF  = rm -rf $(BUILD)
endif

.PHONY: all run clean splashlogo

all: $(DISK)

# ---- kernel C objects ----
$(BUILD)/%.o: $(KERNEL_DIR)/%.c $(wildcard $(INC_DIR)/*.h)
	$(MKDIR_P)
	$(CC) $(K_CFLAGS) -c $< -o $@

# ---- kernel assembly (long-mode entry + interrupt stubs) ----
$(BUILD)/start.o: $(KERNEL_DIR)/arch/x86_64/start.S
	$(MKDIR_P)
	$(AS) $(K_ASFLAGS) $< -o $@

$(BUILD)/interrupt.o: $(KERNEL_DIR)/arch/x86_64/interrupt.S
	$(MKDIR_P)
	$(AS) $(K_ASFLAGS) $< -o $@

$(BUILD)/switch.o: $(KERNEL_DIR)/arch/x86_64/switch.S
	$(MKDIR_P)
	$(AS) $(K_ASFLAGS) $< -o $@

# ---- kernel link + strip to raw binary ----
$(KERNEL_ELF): $(K_ASM_OBJ) $(K_OBJS) $(KERNEL_DIR)/arch/x86_64/kernel.ld
	$(LD) -T $(KERNEL_DIR)/arch/x86_64/kernel.ld -o $@ $(K_ASM_OBJ) $(K_OBJS)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

# ---- bootloader ----
# boot.bin  : ISO variant (El Torito no-emulation, loaded at 0x7C00).  The copy
#             length is injected so the loader moves the WHOLE kernel.
$(BOOT_OBJ): $(BOOT_DIR)/boot.S $(KERNEL_BIN)
	$(MKDIR_P)
	$(AS) $(B_ASFLAGS) -DKERNEL_COPY_BYTES=$(KERNEL_COPYB) -DKERNEL_SECTORS=$(KERNEL_SECTORS) $< -o $@

$(BOOT_ELF): $(BOOT_OBJ) $(BOOT_DIR)/boot.ld
	$(LD) -T $(BOOT_DIR)/boot.ld -o $@ $(BOOT_OBJ)

$(BOOT_BIN): $(BOOT_ELF)
	$(OBJCOPY) -O binary $< $@

# stage2.bin: disk variant (DISK_BOOT), read count injected as a sector total.
$(STAGE2_OBJ): $(BOOT_DIR)/boot.S $(KERNEL_BIN)
	$(MKDIR_P)
	$(AS) $(B_ASFLAGS) -DDISK_BOOT -DKERNEL_COPY_BYTES=$(KERNEL_COPYB) -DKERNEL_SECTORS=$(KERNEL_SECTORS) $< -o $@

$(STAGE2_ELF): $(STAGE2_OBJ) $(BOOT_DIR)/boot.ld
	$(LD) -T $(BOOT_DIR)/boot.ld -o $@ $(STAGE2_OBJ)

$(STAGE2_BIN): $(STAGE2_ELF)
	$(OBJCOPY) -O binary $< $@

# mbr.bin   : LBA 0 (partition table + stage2 loader)
$(MBR_OBJ): $(BOOT_DIR)/mbr.S
	$(MKDIR_P)
	$(AS) $(B_ASFLAGS) $< -o $@

$(MBR_ELF): $(MBR_OBJ) $(BOOT_DIR)/boot.ld
	$(LD) -T $(BOOT_DIR)/boot.ld -o $@ $(MBR_OBJ)

$(MBR_BIN): $(MBR_ELF)
	$(OBJCOPY) -O binary $< $@

# ---- user programs (M2: init + worker, each a separate ELF) ----
USER_DIR    := user
USER_LIB    := $(USER_DIR)/lib
INIT_ELF    := $(BUILD)/init.elf
WORKER_ELF  := $(BUILD)/worker.elf
USER_C      := $(KERNEL_DIR)/user/user_elf.c

USER_INIT_OBJS := $(BUILD)/user/init.o \
                  $(BUILD)/user/lib/syscall.o \
                  $(BUILD)/user/lib/string.o \
                  $(BUILD)/user/crt0.o

USER_WORKER_OBJS := $(BUILD)/user/worker.o \
                    $(BUILD)/user/lib/syscall.o \
                    $(BUILD)/user/lib/string.o \
                    $(BUILD)/user/crt0.o

# User-space flags: freestanding, no libc, FPU/SIMD off (the kernel does not
# touch FPU state, so stay away from it).  Red-zone is kept (standard for
# ring-3 code; interrupts switch to the kernel stack and never clobber it).
U_CFLAGS  := -target $(TARGET) -ffreestanding -nostdlib \
             -mno-sse -mno-sse2 -mno-avx -mno-mmx \
             -Wall -Wextra -O2 -I$(USER_LIB)

$(BUILD)/user/%.o: $(USER_DIR)/%.c
	$(MKDIR_P)
	$(CC) $(U_CFLAGS) -c $< -o $@

$(BUILD)/user/%.o: $(USER_DIR)/%.S
	$(MKDIR_P)
	$(CC) -target $(TARGET) -c $< -o $@

$(INIT_ELF): $(USER_INIT_OBJS) $(USER_DIR)/user.ld
	$(LD) -T $(USER_DIR)/user.ld -o $@ $(USER_INIT_OBJS)

$(WORKER_ELF): $(USER_WORKER_OBJS) $(USER_DIR)/user.ld
	$(LD) -T $(USER_DIR)/user.ld -o $@ $(USER_WORKER_OBJS)

# Embed BOTH user programs into a single generated C array the kernel links in.
$(USER_C): $(INIT_ELF) $(WORKER_ELF) tools/embed_elf.py
	$(PY) tools/embed_elf.py user_elf_init $(INIT_ELF) user_elf_worker $(WORKER_ELF) $(USER_C)

# ---- disk image (MBR + stage2 + kernel) ----
$(DISK): $(MBR_BIN) $(STAGE2_BIN) $(KERNEL_BIN) tools/mkimage.py
	$(PY) tools/mkimage.py $(MBR_BIN) $(STAGE2_BIN) $(KERNEL_BIN) $(DISK)

# ---- vmdk (VMware) ----
vmdk: $(DISK)
	qemu-img convert -f raw -O vmdk $(DISK) $(BUILD)/berryos.vmdk

# ---- regenerate the boot-splash wordmark asset (needs Python + Pillow) ----
splashlogo:
	$(PY) tools/make_splash_logo.py --src tools/assets/splash_logo.png \
	  --out $(KERNEL_DIR)/drivers/splash_logo.c

# ---- run ----
run: $(DISK)
	qemu-system-x86_64 -cpu max \
	  -drive file=$(DISK),format=raw,if=ide \
	  -serial stdio -vga std -display default -no-reboot

clean:
	$(RM_RF)
