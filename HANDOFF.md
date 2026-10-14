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

## Findings, part 3 (Windows capture 2, 2026-10-14)

14. **Driver key**: `HKLM\SOFTWARE\WOW6432Node\Guillemot Corporation\DJHERCULESMIX Series Drivers`
    holds one empty subkey `2.HDJS.2023` (no values). Serato reads only that (Procmon).
    `2023.2.0.0` in its log is derived from the name. Serato 4.0.3 (Wine) logs
    "Driver is valid, update available" for 2.HDJS.2023 (Windows ran 4.0.2: "up to date").
    Non-blocking. Created by `tools/setup-prefix.sh` via a .reg import (`reg add` adds `@=""`).
15. **Hercules native handshake is plain MIDI SysEx** (manufacturer `00 01 4E`):
    host sends `F0 00 01 4E 1C 02 04 7F 00.. F7` and `F0 00 01 4E 1C 01 7D 7F 00.. F7`; device
    answers `..1C 05..` and `..1C 03 7D 7F 00 00 <7-bit pairs: fw 01 42, serial, manuf F0> F7`.
    No vendor control requests in the USB capture. Verified identical via `amidi` on Linux.
16. **Login was the MIDI blocker**: without a Serato account session the MIDI thread stops after
    `[Midi] New MIDI connection`. After login: RtMidi opens, `Hercules Connection: Firmware 1.66`.
    Login callback is `seratodjpro://`; handled by `~/.local/share/applications/serato-djpro-url.desktop`
    -> `tools/serato-launch.sh %u`.
17. **ASIO**: Windows uses `DJControl Inpulse 500 ASIO`, CLSID `{993E67E0-7470-45DC-928C-85F51337A000}`
    (`hdjusbaudioasio_x64.dll`). Serato matches by key name, so the setup registers wineasio
    (AUR 1.3.0) under that name. Wine 11 quirks: `wineasio-register` needs `wine64` (gone); the PE's
    builtin name is `wineasio.dll` but COM registers `wineasio64.dll` -> both copied to system32,
    builtin provided through `WINEDLLPATH=~/.local/lib/wine-serato-hw/dlls` (set by serato-launch.sh).
18. **wineasio init crashed** (fixed by joining group `realtime`) (`tools/probes/asio_probe.c` reproduces it):
    wineasio calls `mlockall()`; with memlock 8 MB, PipeWire's data-loop thread stack mmap fails
    (EAGAIN), `pw_data_loop_start: can't create thread`, then NULL deref in `jack_client_open`.
    Fix: user in group `realtime` (realtime-privileges: memlock unlimited) and re-login.
19. Discarded: Hercules-style MIDI interface path (`hdjusbaudio_enum#...&ks#...`) changes nothing
    (experiment removed from proxy.c).

## Findings, part 4 (2026-10-14): it works

20. **End to end OK**: `***CONNECTED***` -> `kConnectedMode` -> `Hercules Connection: Firmware 1.66`
    -> ASIO `DJControl Inpulse 500 ASIO` running at 48 kHz. Serato asks for 960 frames, wineasio
    (fixed buffersize) refuses, PortAudio corrects to the JACK quantum (1024). Audio confirmed audible.
    The "driver update available" notice is cosmetic (see 14).
21. **wineasio + pipewire-jack crash**: the JACK latency / buffer-size / sample-rate callbacks run
    on PipeWire threads unknown to Wine; wineasio called the ASIO host from them, so any graph
    change (a link, another app playing) killed Serato (`Exception c0000005 in system thread`,
    upstream issue #132, unfixed in master). Serato also restarts the driver on every
    `kAsioLatenciesChanged`, which PipeWire triggers on any graph change.
22. **Fixed by `patches/wineasio-1.3.0-pipewire.patch`** (built by `tools/build-wineasio.sh`,
    installed as builtin `wineasio.dll` in `~/.local/lib/wine-serato-hw/dlls`): callbacks only set
    flags, a Wine thread delivers them; latency changes reported only when the port latency really
    changes; `WINEASIO_OUTPUT_PORTS` / `WINEASIO_INPUT_PORTS` regexes restrict "Connect to hardware".
23. **Routing**: pipewire-jack names physical ports `<node description>:<port>` (not the alias).
    `serato-launch.sh` sets the regexes to the Inpulse ports: out_1-2 -> playback_FL/FR (master),
    out_3-4 -> playback_RL/RR (headphones), capture_FL/FR -> in_1-2. Verified: live relinks no
    longer crash or restart audio.
24. Serato logs are in `~/Music/_Serato_/Logs`.
25. Killing Serato (`wineserver -k`) while it holds the MIDI port can hang the controller: LEDs off,
    no MIDI events, no SysEx reply, but still enumerated over USB (Serato then logs CONNECTED but no
    `Hercules Connection`). Fix: unplug/replug the USB cable.

## Next step

1. Confirm by ear that the headphone cue (out_3-4) reaches the Inpulse headphone jack and master
   the main outputs.
2. Package for GitHub: README (requirements: realtime group, pipewire-jack, wine 11, mingw), license,
   install flow (`tools/setup-prefix.sh`, `.desktop` + `seratodjpro://` handler).
3. Optional: offer the wineasio patch upstream (issue #132).

## Repository layout

- `src/proxy.c`: winmm proxy.
- `src/libusb_shim.c`, `src/libusb_abi.h`: sysfs-backed libusb-1.0.dll replacement.
- `tools/gen_exports.py`: generates `build/{winmm.def,thunks.S,exports.h}` from Wine's winmm.dll.
- `tools/probes/`: small diagnostic programs (build with `x86_64-w64-mingw32-gcc ... -lwinmm` / `-lsetupapi`).
- `tools/windows-capture/`: the Windows 10 capture kit.
- `research/logs/`: zstd-compressed Serato/Wine traces (gitignored: contain local paths).

Build deps: `mingw-w64-gcc`, `wine`, `python3`.
