# wine-serato-hw: research handoff

Goal: make Serato DJ Pro (Windows build, running under Wine on Linux) recognize a
Hercules DJControl Inpulse 500 (USB `06f8:b12b`) as supported hardware, and
publish the result on GitHub so others can use it. No cracks or patches to
Serato's binary: interoperability layers only (proxy DLLs, registry, Wine-side fixes).

## Environment (as of 2026-10-14)

- Host: CachyOS, kernel 6.18, PipeWire (+ pipewire-jack), Wine 11.18.
- Wine prefix: `~/.wine-serato` (win64). Serato DJ Pro 4.0.3 installed at
  `C:\Program Files\Serato\Serato DJ Pro\` (official signed installer).
- Serato is protected with VMProtect (`.vmp0` section): never touch its binary.
- App launcher entry: `~/.local/share/applications/wine/Programs/Serato/Serato DJ Pro .desktop`
  (does NOT load the proxy yet; it runs plain Serato).
- Controller on Linux: all 4 USB interfaces bound to `snd-usb-audio`
  (0 = audio control, 1/2 = audio streaming, 3 = MIDI streaming). No HID, no serial number.
  ALSA card 3 (`D500`), MIDI port `DJControl Inpulse 500 MIDI 1`.

## How to run Serato with the proxy

```sh
cd ~/Projects/wine-serato-hw && make
cp build/winmm.dll build/libusb-1.0.dll "$HOME/.wine-serato/drive_c/Program Files/Serato/Serato DJ Pro/"
cd "$HOME/.wine-serato/drive_c/Program Files/Serato/Serato DJ Pro"
WINEPREFIX=$HOME/.wine-serato WINEDLLOVERRIDES="winmm=n,b" WINE_SERATO_HW_LOG=1 \
  wine "Serato DJ Pro.exe" 2>&1 | tee /tmp/serato.log
```

Serato's own log (glog) goes to stderr. Useful filters:
`rg "wine-serato-hw|\[Device\]|\[Midi\]" /tmp/serato.log | rg -v "WasapiBase|Connected audio device"`.
For device-tree tracing add `WINEDEBUG=+setupapi,+reg` (huge output; Serato's main thread was `0024`).

## Findings

1. **Serato identifies MIDI hardware by USB VID/PID** taken from the WinMM device
   interface path: it calls `midiInMessage/midiOutMessage(DRV_QUERYDEVICEINTERFACE[SIZE])`
   and applies the regex `^.*VID_([0-9A-F]{4}).*PID_([0-9A-F]{4}).*$`.
2. **Wine does not implement that query**: by device ID it returns
   `MMSYSERR_INVALHANDLE` (5); with an open handle, `MMSYSERR_NOTSUPPORTED` (8).
   So Serato logs `vid=0000&pid=0000` and treats the controller as generic MIDI.
3. **Fixed by `src/proxy.c`** (winmm.dll proxy, forwards 187 exports to Wine's builtin,
   overrides only `midiInMessage`/`midiOutMessage`). It maps the MIDI port name to an
   ALSA card via `/proc/asound/cards` and `/proc/asound/cardN/usbid` (read through `Z:`)
   and returns
   `\\?\USB#VID_06F8&PID_B12B&MI_00#WINE&ALSA&CARD3#{6994ad04-93ef-11d0-a3cc-00a0c9223196}\global`.
   Result: Serato logs `vid=06f8&pid=b12b` and stops treating it as generic MIDI
   (no more generic "Trying to aggregate" / "Device Added" for it).
   Known inaccuracy: the MIDI interface on this device is `MI_03`, not `MI_00`.
4. **But Serato does not activate the controller.** It keeps the MIDI connection in a
   remove/re-add loop (~15 s) and the UI shows nothing.
5. Serato's device aggregation is **USB-device-centric** (`DeviceAggregator`,
   string: `Aggregation failed (can't find MIDI or HID endpoints) for USB device`):
   - `SetupDiGetClassDevs(NULL, "USB", DIGCF_PRESENT|DIGCF_ALLCLASSES)`: Wine returns
     no USB devices at all (wineusb skips devices claimed by Linux kernel drivers).
   - Enumerates HID interfaces and walks `CM_Get_Parent` up to a USB ancestor.
   - For HID it also calls `CM_Get_Device_Interface_PropertyW` and
     `CM_Get_DevNode_PropertyW({a45c254e-...},4)`.
   - **It never queries setupapi/cfgmgr32 with the MIDI interface path we return.**
     How it links the MIDI port to a USB device is still unknown (maybe the
     `unique_group` hash in its device address).
6. **Fake devnodes work**: `tools/probes/fake_usb_devnode.c` registers
   `USB\VID_06F8&PID_B12B\winealsa3` via `SetupDiCreateDeviceInfo` +
   `SetupDiRegisterDeviceInfo`. Wine reports it as present and Serato enumerates it.
   On that node Serato reads: `Driver` (SPDRP_DRIVER, empty), then
   `Device Parameters\DeviceInterfaceGUIDs` / `DeviceInterfaceGUID` (WinUSB-style),
   then `CM_Get_Parent`.
7. **Wine stores the parent link as a registry property**:
   `Enum\<id>\Properties\{4340A6C5-93FA-4706-972C-7B648008A5A7}\0008` (DEVPKEY_Device_Parent).
   So a full fake tree (USB parent -> MI_xx children) can be built from the registry.
8. Audio: Serato sees the Inpulse outputs via WASAPI ("Windows Audio":
   `Speakers (DJControl Inpulse 500 Analog Surround 4.0)`), but there is no ASIO
   driver in the prefix. Not investigated yet.

## Current state of the prefix (experiment leftovers)

- `C:\Program Files\Serato\Serato DJ Pro\winmm.dll` = proxy (inactive unless
  `WINEDLLOVERRIDES="winmm=n,b"`).
- Fake devnode `HKLM\System\CurrentControlSet\Enum\USB\VID_06F8&PID_B12B\winealsa3`
  with an empty `Device Parameters` key. Remove with
  `wine reg delete 'HKLM\System\CurrentControlSet\Enum\USB\VID_06F8&PID_B12B' /f`.

## Windows capture results (2026-10-14)

Capture in `/mnt/ssd/wine-serato-hw-capture/output/`. Key facts:

- On Windows the Hercules vendor driver owns the device:
  `USB\VID_06F8&PID_B12B\<id>` (usbccgp) -> `USB\...&MI_00` (HDJusbaudio) ->
  `HDJUSBAUDIO_ENUM\VID_06F8&PID_B12B&KS\<id>` (KS: MIDI + audio endpoints). No HID.
- Real MIDI interface path (by device ID only; by handle returns 8 on Windows too):
  `\\?\hdjusbaudio_enum#vid_06f8&pid_b12b&ks#8&1c9ebe79&0&0000#{6994ad04-...}\midi_in_00_00`.
  Our `USB#VID_..` path is enough: Serato only regexes VID/PID out of it.
- MIDI port name on Windows is `DJControl Inpulse 500` (Wine/ALSA: `... MIDI 1`).
- Serato's working sequence (Windows log): `New USB Connection from the module:
  Vid=0x6f8&Pid=0xb12b&BcdDevice=0x200&Instance=<bus>#<addr>` -> aggregate
  `06f8_b12b_0000#0` -> `Got MIDI connection` -> `***CONNECTED***` -> RtMidi open ->
  `CNativeMixer kConnectedMode` -> `Hercules Connection: Firmware/Serial/Driver Version`
  -> audio via ASIO driver named `DJControl Inpulse 500 ASIO`.

## Findings, part 2

9. **USB enumeration is libusb**: Serato imports 9 read-only functions from the
   app-local `libusb-1.0.dll` (1.0.23, LGPL, not VMProtect'ed): init/exit,
   get/free_device_list, get_device_descriptor, get/free_config_descriptor,
   get_bus_number, get_device_address.
10. **Fixed by `src/libusb_shim.c`**: clean-room `libusb-1.0.dll` that enumerates
    `/sys/bus/usb/devices` through `Z:`. Serato now logs "New USB device plugged-in:
    Hercules DJControl Inpulse 500". Original kept as `libusb-1.0.dll.orig`.
11. **Port name fixed in the proxy** (`midi{In,Out}GetDevCaps{A,W}` drop the ALSA
    " MIDI 1" suffix for USB cards). With 9+10+11 Serato logs
    `***CONNECTED*** Hercules DJControl Inpulse 500` and `Device Added`.
12. **Current blocker: Hercules driver check.** Serato shows "Driver not installed" and
    the MIDI connection thread exits right after `[Midi] New MIDI connection`. It opens
    `HKLM\SOFTWARE\WOW6432Node\Guillemot Corporation\DJHERCULESMIX Series Drivers`,
    calls `RegQueryInfoKeyW` (subkey count) and `RegEnumKeyExW` (subkey names), so it
    parses subkey names (probably a driver version). Real values unknown.
13. WMI queries (Win32_BIOS / Win32_DiskDrive serials) seen in loops are Serato's
    licensing fingerprint: unrelated, do not touch.

## Next step

The user is running the second Windows capture (`tools/windows-capture/capture2.bat`
+ `capture2.ps1`, copies in `/mnt/ssd/wine-serato-hw-capture/`). Expected in
`/mnt/ssd/wine-serato-hw-capture/output2/`:

- `guillemot*.reg`, `hercules*.reg`: the `DJHERCULESMIX Series Drivers` key (blocker #12).
- `asio*.reg`, `asio_clsids.txt`: Hercules ASIO name, CLSID, DLL path.
- `drivers_services.txt`, `uninstall.txt`, `files.txt`: Hercules drivers, services, tray apps.
- `serato_hkcu.reg`, `serato_logs/`: Serato settings and logs of the captured session.
- `procmon.csv`: Process Monitor log of Serato (filter: Serato DJ Pro.exe) during a full
  connect + play session. Look for the Guillemot key reads, Hercules files/DLLs, and
  DeviceIoControl on `hdjusbaudio_enum` paths.
- `inpulse.pcapng`: USBPcap capture of the same session. Shows whether the
  "Hercules Connection: Firmware/Serial/Driver Version" handshake is MIDI SysEx
  (works through ALSA) or vendor control requests via the driver (needs emulation).

Plan after analysis:
1. Recreate the `DJHERCULESMIX Series Drivers` key with a setup tool (not by hand).
2. Get past the Hercules native connection (MIDI thread currently exits after
   `[Midi] New MIDI connection`).
3. Audio: Serato wants an ASIO driver named `DJControl Inpulse 500 ASIO`; likely
   register wineasio under that name.
4. Package everything (setup script, README) for GitHub.

## Repository layout

- `src/proxy.c`: winmm proxy.
- `src/libusb_shim.c`, `src/libusb_abi.h`: sysfs-backed libusb-1.0.dll replacement.
- `tools/gen_exports.py`: generates `build/{winmm.def,thunks.S,exports.h}` from Wine's winmm.dll.
- `tools/probes/`: small diagnostic programs (build with `x86_64-w64-mingw32-gcc ... -lwinmm` / `-lsetupapi`).
- `tools/windows-capture/`: the Windows 10 capture kit.
- `research/logs/`: zstd-compressed Serato/Wine traces (gitignored: contain local paths).

Build deps: `mingw-w64-gcc`, `wine`, `python3`.
