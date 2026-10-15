# wine-serato-hw

Serato DJ Pro (Windows) on Linux with Wine, with a USB DJ controller recognized as
supported Serato hardware (no MIDI mapping needed) and audio going through the
controller's own outputs.

**Tested with:** Hercules DJControl Inpulse 500. Other controllers should work with
their own profile (see below), but nobody has tried yet.

Serato itself is not modified. Everything here sits around it:

- `src/proxy.c`: a `winmm.dll` proxy. Wine can't tell Serato the USB VID/PID of a
  MIDI port and names it "... MIDI 1"; the proxy fixes both.
- `src/libusb_shim.c`: a replacement for the `libusb-1.0.dll` Serato ships, backed
  by `/sys/bus/usb`, so Serato sees the controller plugged in.
- The registry keys Serato checks for the vendor driver, if any.
- wineasio with a patch (`patches/`) so it survives PipeWire, registered under
  the name of the vendor ASIO driver.

Tested on CachyOS, Wine 11.18, PipeWire 1.6 with pipewire-jack, Serato DJ Pro 4.0.3.

## Requirements

- Wine (64-bit prefix), `winegcc`/`winebuild` and Wine headers
- `mingw-w64-gcc`, `python3`, `make`, `curl`, `patch`
- PipeWire with pipewire-jack
- Your user in a group with unlimited memlock (`realtime` on Arch). wineasio calls
  `mlockall()` and crashes otherwise. Log out and back in after adding it.
- Serato DJ Pro installed in the prefix with the official installer, and a Serato account

## Setup

```sh
sudo usermod -aG realtime $USER   # then re-login; `ulimit -l` must say unlimited
WINEPREFIX=~/.wine-serato tools/setup-prefix.sh hercules-djcontrol-inpulse-500
tools/serato-launch.sh
```

`setup-prefix.sh` takes a profile from `profiles/`. It builds the DLLs, copies them
next to Serato, adds the driver registry keys, builds and registers wineasio, and
registers the `seratodjpro://` handler used by the login. Log in once inside
Serato; without a session it won't talk to the controller.

## Audio routing

wineasio connects only to the controller's ports, in order. For the Inpulse 500:

| ASIO | Inpulse |
|------|---------|
| out 1-2 | master |
| out 3-4 | headphones |
| in 1-2 | inputs |

`WINEASIO_OUTPUT_PORTS` / `WINEASIO_INPUT_PORTS` in the environment override the profile.

## Other controllers

Copy `profiles/TEMPLATE.conf` and fill in:

- `ASIO_NAME`: run Serato once with the controller plugged in and look in
  `~/Music/_Serato_/Logs` for `Attempting to get connection, name=...`.
- `WINEASIO_OUTPUT_PORTS` / `WINEASIO_INPUT_PORTS`: regexes on the JACK port names.
  pipewire-jack uses `<device description>:<port>`, e.g.
  `DJControl Inpulse 500 Analog Surround 4.0:playback_FL`.
- `DRIVER_KEYS`: only if Serato says "Driver not installed". Finding them needs a
  Windows machine with the vendor driver (`tools/windows-capture/` helps).

Controllers that talk HID instead of MIDI, or whose audio isn't class compliant
(not picked up by `snd-usb-audio`), probably won't work. If you get one working,
a PR with its profile is welcome.

## Known issues

- Serato says a driver update is available. Harmless.
- Serato crashes when quitting under Wine and, with ASIO active, hangs afterwards.
  The launcher stops it 20 s after it starts shutting down (`SERATO_QUIT_GRACE`).
- Serato sometimes crashes while starting, with the same hang. The launcher stops
  it as soon as the crash is recorded and, within the first 90 s
  (`SERATO_START_WINDOW`), starts it again once.
- If Serato is killed while it holds the MIDI port, the controller can hang (LEDs
  off, no MIDI). Replug the USB cable.

`HANDOFF.md` has the full research notes.

## License

MIT for this repository's code. The wineasio patch follows wineasio's LGPL-2.1.
Serato and Hercules are trademarks of their owners; this project is not affiliated
with them.
