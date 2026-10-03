# =============================================================================
#  FRDM-MCXN947 — Bare Metal Firmware
#
#  Gereksinimler : arm-none-eabi-gcc (PATH'te) + GNU make
#  Flash icin    : NXP LinkServer
#
#  Kullanim:
#    make            derle
#    make flash      karta yukle
#    make clean      build ciktilarini sil
#    make size       bellek kullanimini goster
#
#  Toolchain PATH'te degilse:
#    make ARMGCC_DIR=/c/path/to/arm-gnu-toolchain
# =============================================================================

TARGET := mcxn947-bare-metal
BUILD  := build

# ---- Toolchain -------------------------------------------------------------
ifdef ARMGCC_DIR
  CROSS := $(ARMGCC_DIR)/bin/arm-none-eabi-
else
  CROSS := arm-none-eabi-
endif

CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size

# ---- Hedef cip -------------------------------------------------------------
# Cortex-M33, tek duyarlikli donanim FPU
CPU  := -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16
DEFS := -DCPU_MCXN947VDF_cm33_core0 -DMCXN947_cm33_core0_SERIES -D__STARTUP_CLEAR_BSS

# ---- Kaynaklar -------------------------------------------------------------
# Katmanlar: driver -> hal -> bsp -> scheduler -> application
# Yeni .c dosyasi eklediginde bu listeye DOKUNMANA GEREK YOK (wildcard).
SRC_C := $(wildcard src/*.c)                \
         $(wildcard src/driver/*.c)         \
         $(wildcard src/hal/*.c)            \
         $(wildcard src/bsp/*.c)            \
         $(wildcard src/scheduler/*.c)      \
         $(wildcard src/application/*.c)    \
         vendor/MCXN947/system_MCXN947_cm33_core0.c

SRC_S := vendor/MCXN947/startup_MCXN947_cm33_core0.S

INCLUDES := -Isrc                    \
            -Isrc/driver             \
            -Isrc/hal                \
            -Isrc/bsp                \
            -Isrc/scheduler          \
            -Isrc/application        \
            -Ivendor/CMSIS           \
            -Ivendor/MCXN947         \
            -Ivendor/MCXN947/periph

LDSCRIPT := vendor/MCXN947/MCXN947_cm33_core0_flash.ld

# ---- Derleyici bayraklari --------------------------------------------------
#  -Og -g3      : debug dostu optimizasyon (adim adim izlenebilir)
#  -ffunction-sections / -fdata-sections + --gc-sections
#               : kullanilmayan fonksiyon/veriyi at
#  -MMD -MP     : header bagimliligi takibi (.d dosyalari)
CFLAGS := $(CPU) $(DEFS) $(INCLUDES) \
          -std=gnu11 -Og -g3        \
          -Wall -Wextra             \
          -ffunction-sections -fdata-sections -fno-common \
          -MMD -MP

ASFLAGS := $(CPU) $(DEFS) -g3 -x assembler-with-cpp

LDFLAGS := $(CPU) -T$(LDSCRIPT)        \
           --specs=nano.specs          \
           --specs=nosys.specs         \
           -Wl,--gc-sections           \
           -Wl,-Map=$(BUILD)/$(TARGET).map \
           -Wl,--print-memory-usage    \
           -Wl,--no-warn-rwx-segments

# ---- Nesne dosyalari -------------------------------------------------------
OBJ := $(addprefix $(BUILD)/,$(SRC_C:.c=.o)) $(addprefix $(BUILD)/,$(SRC_S:.S=.o))
DEP := $(OBJ:.o=.d)

# ---- Flash (LinkServer) ----------------------------------------------------
LINKSERVER ?= LinkServer
DEVICE     ?= MCXN947:FRDM-MCXN947
PROBE      ?=
ifeq ($(PROBE),)
  PROBE_ARG :=
else
  PROBE_ARG := --probe $(PROBE)
endif

# =============================================================================

.PHONY: all clean flash erase size
.DEFAULT_GOAL := all

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin $(BUILD)/$(TARGET).hex

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC   $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	@echo "  AS   $<"
	@$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJ) $(LDSCRIPT)
	@echo "  LD   $@"
	@$(CC) $(OBJ) $(LDFLAGS) -o $@
	@$(SIZE) $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	@$(OBJCOPY) -O binary $< $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	@$(OBJCOPY) -O ihex $< $@

size: $(BUILD)/$(TARGET).elf
	@$(SIZE) -A $<

flash: $(BUILD)/$(TARGET).elf
	$(LINKSERVER) flash $(PROBE_ARG) $(DEVICE) load $<

erase:
	$(LINKSERVER) flash $(PROBE_ARG) $(DEVICE) erase

clean:
	@rm -rf $(BUILD)
	@echo "temizlendi."

-include $(DEP)
