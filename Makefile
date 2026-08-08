CROSS_COMPILE ?= arm-none-eabi-

CC      := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
SIZE    := $(CROSS_COMPILE)size

FEATURES   := uart crypto mmc
COMMON_DIR := payload/mtk-payloads/common

include $(COMMON_DIR)/common.mk

OUTPUT_DIR := ./bin
BUILD_DIR  := payload/build

ASM_SRCS   := payload/src/start.S

MAIN_SRCS   := payload/src/main.c payload/src/gpt.c $(EXTRA_COMMON_SRCS) $(COMMON_SRCS)
PATCH_SRCS  := payload/src/patch.S
RESCUE_SRCS := payload/src/recovery_to_boot.c payload/src/gpt.c $(EXTRA_COMMON_SRCS) $(COMMON_SRCS)

CFLAGS := $(COMMON_CFLAGS) \
    -mthumb -mcpu=cortex-a9 \
    -fno-stack-protector \
    -fPIE \
    -Ipayload/include \
    $(COMMON_INCLUDES)

ASMFLAGS := -Ipayload/include \
    $(COMMON_INCLUDES)

LDFLAGS := $(COMMON_LDFLAGS) \
    -Tpayload/src/generic.ld -lgcc

c_obj   = $(patsubst %.c,$(BUILD_DIR)/%.o,$(subst ../,,$(1)))
asm_obj = $(patsubst %.S,$(BUILD_DIR)/%.o,$(1))

ASM_OBJS    := $(call asm_obj,$(ASM_SRCS))
MAIN_OBJS   := $(call c_obj,$(MAIN_SRCS)) $(ASM_OBJS)
PATCH_OBJS  := $(call c_obj,$(PATCH_SRCS)) $(ASM_OBJS)
RESCUE_OBJS := $(call c_obj,$(RESCUE_SRCS)) $(ASM_OBJS)

TARGET_MAIN_ELF   := $(BUILD_DIR)/unlock.elf
TARGET_MAIN_BIN   := $(OUTPUT_DIR)/unlock.bin

TARGET_PATCH_ELF  := $(BUILD_DIR)/patch.elf
TARGET_PATCH_BIN  := $(OUTPUT_DIR)/patch.bin

TARGET_RESCUE_ELF := $(BUILD_DIR)/recovery-to-boot.elf
TARGET_RESCUE_BIN := $(OUTPUT_DIR)/recovery-to-boot.bin

.PHONY: all rescue clean

all: $(TARGET_MAIN_BIN) $(TARGET_PATCH_BIN)

rescue: $(TARGET_RESCUE_BIN)

$(TARGET_MAIN_ELF): LDFLAGS += -Wl,-u,__aeabi_uidiv
$(TARGET_RESCUE_ELF): LDFLAGS += -Wl,-u,__aeabi_uidiv

$(TARGET_MAIN_BIN): $(TARGET_MAIN_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@.tmp
	( dd if=/dev/zero bs=512 count=1 status=none; cat $@.tmp ) > $@
	rm -f $@.tmp
	@echo "Built: $@"

$(TARGET_MAIN_ELF): $(MAIN_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $^ -o $@ $(LDFLAGS)
	$(SIZE) $@

$(TARGET_PATCH_BIN): $(TARGET_PATCH_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@.tmp
	( dd if=/dev/zero bs=512 count=1 status=none; cat $@.tmp ) > $@
	rm -f $@.tmp
	@echo "Built: $@"

$(TARGET_PATCH_ELF): $(PATCH_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $^ -o $@ $(LDFLAGS)
	$(SIZE) $@

$(TARGET_RESCUE_BIN): $(TARGET_RESCUE_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@.tmp
	( dd if=/dev/zero bs=512 count=1 status=none; cat $@.tmp ) > $@
	rm -f $@.tmp
	@echo "Built: $@"

$(TARGET_RESCUE_ELF): $(RESCUE_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $^ -o $@ $(LDFLAGS)
	$(SIZE) $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASMFLAGS) -c $< -o $@

$(eval $(call common_build_rule,$(BUILD_DIR),$(CC),$(CFLAGS)))

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(TARGET_MAIN_BIN) $(TARGET_PATCH_BIN) $(TARGET_RESCUE_BIN)
