CC      := x86_64-w64-mingw32-gcc
OBJDUMP := x86_64-w64-mingw32-objdump
WINE_WINMM ?= /usr/lib/wine/x86_64-windows/winmm.dll

BUILD := build
CFLAGS := -O2 -Wall -Wextra -I$(BUILD)
LDFLAGS := -shared -static-libgcc -Wl,--enable-stdcall-fixup

all: $(BUILD)/winmm.dll $(BUILD)/libusb-1.0.dll

$(BUILD)/exports.h $(BUILD)/winmm.def $(BUILD)/thunks.S: tools/gen_exports.py $(WINE_WINMM)
	$(OBJDUMP) -p $(WINE_WINMM) | python3 -I tools/gen_exports.py $(BUILD)

$(BUILD)/winmm.dll: src/proxy.c $(BUILD)/exports.h $(BUILD)/thunks.S $(BUILD)/winmm.def
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ src/proxy.c $(BUILD)/thunks.S $(BUILD)/winmm.def

$(BUILD)/libusb-1.0.dll: src/libusb_shim.c src/libusb_abi.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ src/libusb_shim.c

$(BUILD)/libusb_probe.exe: tools/probes/libusb_probe.c src/libusb_abi.h
	$(CC) $(CFLAGS) -o $@ tools/probes/libusb_probe.c

clean:
	rm -rf $(BUILD)

.PHONY: all clean
