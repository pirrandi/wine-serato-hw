# wine-serato-hw

Serato DJ Pro (Windows) on Linux with Wine, with a Hercules DJControl Inpulse 500
recognized as supported Serato hardware (no MIDI mapping needed) and audio going
through the controller's own outputs.

Serato itself is not modified. Everything here sits around it:

- `src/proxy.c`: a `winmm.dll` proxy. Wine can't tell Serato the USB VID/PID of a
  MIDI port and names it "... MIDI 1"; the proxy fixes both.
- `src/libusb_shim.c`: a replacement for the `libusb-1.0.dll` Serato ships, backed
  by `/sys/bus/usb`, so Serato sees the controller plugged in.
- A registry key Serato checks for the Hercules driver.
- wineasio with a patch (`patches/`) so it survives PipeWire, registered under
  the name of the Hercules ASIO driver.

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
WINEPREFIX=~/.wine-serato tools/setup-prefix.sh
tools/serato-launch.sh
```

`setup-prefix.sh` builds the DLLs, copies them next to Serato, adds the Hercules
registry key, builds and registers wineasio, and registers the `seratodjpro://`
handler used by the login. Log in once inside Serato; without a session it won't
talk to the controller.

## Audio routing

The launcher points wineasio at the Inpulse ports only:

| ASIO | Inpulse |
|------|---------|
| out 1-2 | master |
| out 3-4 | headphones |
| in 1-2 | inputs |

Override `WINEASIO_OUTPUT_PORTS` / `WINEASIO_INPUT_PORTS` (regexes on JACK port
names, see `pw-link -io`) for another device.

## Known issues

- Serato says a driver update is available. Harmless.
- If Serato is killed while it holds the MIDI port, the controller can hang (LEDs
  off, no MIDI). Replug the USB cable.
- Only the Inpulse 500 has been tested. Other Hercules controllers likely need
  their own driver key and port names.

`HANDOFF.md` has the full research notes.

## License

MIT for this repository's code. The wineasio patch follows wineasio's LGPL-2.1.
Serato and Hercules are trademarks of their owners; this project is not affiliated
with them.
