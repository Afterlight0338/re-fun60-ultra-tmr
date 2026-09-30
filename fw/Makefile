# Open firmware for the MonsGeek FUN60 Ultra TMR (RY5088 / AT32F405, dev_id 2307).
#
#   make            -> build/fun60.bin (app) and build/fun60_dfu.bin (with the 512-byte
#                      chip-ID header the factory bootloader checks; flash this one)
#   make test       -> host tests (protocol, descriptors)
#   make vendor     -> fetch Artery's official AT32F402/405 library at the pinned commit
#
# Flash: sudo ../flasher/target/release/ry-flash --image build/fun60_dfu.bin --custom --arm
# Restore stock: sudo ../flasher/target/release/ry-flash --image ../stock/2307/fw_at32.bin --arm

BSP_REPO   = https://github.com/ArteryTek/AT32F402_405_Firmware_Library
BSP_COMMIT = 5dd9d55a2ce9ffa8fe0cb2652ac142920f2094a3   # v2.1.5
BSP        = vendor/at32
CMSIS      = $(BSP)/libraries/cmsis/cm4
DRV        = $(BSP)/libraries/drivers
USBDRV     = $(BSP)/middlewares/usb_drivers

CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
NM      = arm-none-eabi-nm
SIZE    = arm-none-eabi-size

OUT  = build
APP  = $(OUT)/fun60

DEFS = -DAT32F405RCT7 -DUSE_STDPERIPH_DRIVER -DUSB_OTG_HS
INCS = -Isrc -I$(CMSIS)/core_support -I$(CMSIS)/device_support -I$(DRV)/inc -I$(USBDRV)/inc
ARCH = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS  = $(ARCH) $(DEFS) $(INCS) -Os -g -Wall -ffunction-sections -fdata-sections -MMD -MP
LDFLAGS = $(ARCH) -T app.ld -specs=nano.specs -specs=nosys.specs -Wl,--gc-sections \
          -Wl,-Map=$(APP).map,--print-memory-usage -Wl,--no-warn-rwx-segments

SRC = src/main.c src/scan.c src/power.c src/radio.c src/indicators.c src/proto.c src/usb_desc.c src/usb_hid.c src/keys.c src/debounce.c src/leds.c src/settings.c \
      src/at32f402_405_clock.c \
      $(CMSIS)/device_support/system_at32f402_405.c \
      $(addprefix $(DRV)/src/at32f402_405_, crm.c gpio.c misc.c adc.c flash.c usb.c wdt.c pwc.c spi.c dma.c) \
      $(addprefix $(USBDRV)/src/, usb_core.c usbd_core.c usbd_int.c usbd_sdr.c)
ASM = $(CMSIS)/device_support/startup/gcc/startup_at32f402_405.s
OBJ = $(patsubst %,$(OUT)/obj/%.o,$(notdir $(basename $(SRC) $(ASM))))

vpath %.c src $(CMSIS)/device_support $(DRV)/src $(USBDRV)/src
vpath %.s $(CMSIS)/device_support/startup/gcc

all: $(APP)_dfu.bin

$(OUT)/obj/%.o: %.c | $(OUT)/obj
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/obj/%.o: %.s | $(OUT)/obj
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/obj:
	@mkdir -p $@

$(APP).elf: $(OBJ) app.ld
	$(CC) $(OBJ) $(LDFLAGS) -o $@
	$(SIZE) $@

# Boot-safety checks for the two ways a build has bricked this board (2026-09-27):
# USB on the wrong controller, and no way back into the factory bootloader.
$(APP).bin: $(APP).elf
	$(OBJCOPY) -O binary $< $@
	@$(NM) $< | grep -q ' T OTGHS_IRQHandler$$' || { echo "FAIL: USB IRQ is not OTGHS1"; rm -f $@; false; }
	@od -An -tx4 -v -w4 $@ | grep -q 55aa55aa || { echo "FAIL: no bootloader mailbox magic"; rm -f $@; false; }
	@od -An -tx4 -N8 $@ | awk '{ if ($$1 !~ /^200/ || $$2 !~ /^080/) { print "FAIL: vector table " $$0; exit 1 } }' || { rm -f $@; false; }
	@echo "ok: OTGHS1 IRQ, mailbox magic, vector table $$(od -An -tx4 -N8 $@)"

$(APP)_dfu.bin: $(APP).bin
	@printf 'AT32F405 8KMKB  ' > $(OUT)/hdr.bin && truncate -s 512 $(OUT)/hdr.bin
	@cat $(OUT)/hdr.bin $< > $@ && echo "$@ ready ($$(stat -c %s $@) bytes)"

test: | $(OUT)
	cc -std=c11 -Wall -Wextra -DHOST_TEST -Isrc test/test_host.c src/proto.c src/power.c src/radio.c src/indicators.c src/usb_desc.c src/keys.c src/debounce.c src/leds.c src/settings.c -o $(OUT)/test_host
	$(OUT)/test_host
$(OUT):
	@mkdir -p $@

vendor:
	git clone --filter=blob:none --no-checkout $(BSP_REPO) $(BSP)
	cd $(BSP) && git sparse-checkout init --no-cone && \
	  git sparse-checkout set '/LICENSE*' '/libraries/' '/middlewares/usb_drivers/' \
	    '/project/at_start_f405/examples/usb_device/custom_hid/' && git checkout $(BSP_COMMIT)

clean:
	rm -rf $(OUT)

-include $(OBJ:.o=.d)

.PHONY: all test vendor clean
