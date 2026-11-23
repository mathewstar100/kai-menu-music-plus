# SPDX-License-Identifier: MIT
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment")
endif

TARGET := kai-menu-music
BUILD := build
PREFIX := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-
include $(DEVKITPRO)/libounce/switch2_cpu.mk
ARCH := $(SWITCH2_CPU) -mtp=soft -fPIE
CFLAGS := $(ARCH) -std=gnu11 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -D__SWITCH__ -D__SWITCH2__ \
    -I$(DEVKITPRO)/portlibs/switch2/include -I$(DEVKITPRO)/libounce/include
LDFLAGS := $(ARCH) -specs=$(DEVKITPRO)/libounce/switch.specs -Wl,--gc-sections
LIBS := -L$(DEVKITPRO)/portlibs/switch2/lib -L$(DEVKITPRO)/libounce/lib \
    -lkaihen_plugin -lounce_core

all: $(TARGET).nso $(TARGET)-selector.nro

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: source/%.c source/core.h source/songs.h source/applets.h source/control.h | $(BUILD)
	$(PREFIX)gcc $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(BUILD)/main.o $(BUILD)/core.o $(BUILD)/songs.o $(BUILD)/applets.o
	$(PREFIX)gcc $(LDFLAGS) $^ $(LIBS) -o $@

$(TARGET).nso: $(BUILD)/$(TARGET).elf
	$(DEVKITPRO)/tools/bin/elf2nso $< $@

$(BUILD)/selector.elf: $(BUILD)/selector.o $(BUILD)/core.o $(BUILD)/songs.o
	$(PREFIX)gcc $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD)/selector.nacp: Makefile | $(BUILD)
	$(DEVKITPRO)/tools/bin/nacptool --create "Menu Music" "WeWaffles" "1.1.0" $@

$(TARGET)-selector.nro: $(BUILD)/selector.elf $(BUILD)/selector.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=$(BUILD)/selector.nacp

clean:
	rm -rf $(BUILD) $(TARGET).nso $(TARGET)-selector.nro

.PHONY: all clean
