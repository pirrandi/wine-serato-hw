@echo off
rem Captures how Windows exposes the Hercules DJControl Inpulse 500 (VID_06F8 PID_B12B)
rem so the Wine setup can mirror it. Run as Administrator with the controller plugged in.
setlocal
cd /d "%~dp0"
set OUT=%~dp0output
if not exist "%OUT%" mkdir "%OUT%"

echo [1/7] USB device tree
reg export "HKLM\SYSTEM\CurrentControlSet\Enum\USB" "%OUT%\enum_usb.reg" /y >nul
reg export "HKLM\SYSTEM\CurrentControlSet\Enum\SWD\MMDEVAPI" "%OUT%\enum_swd_mmdevapi.reg" /y >nul
reg export "HKLM\SYSTEM\CurrentControlSet\Enum\SWD\MIDISRV" "%OUT%\enum_swd_midisrv.reg" /y >nul 2>&1

echo [2/7] Device interface classes (audio, MIDI, HID, USB, WinUSB)
for %%G in (
  {6994AD04-93EF-11D0-A3CC-00A0C9223196}
  {65E8773D-8F56-11D0-A3B9-00A0C9223196}
  {65E8773E-8F56-11D0-A3B9-00A0C9223196}
  {DDA54A40-1E4C-11D1-A050-405705C10000}
  {4D1E55B2-F16F-11CF-88CB-001111000030}
  {A5DCBF10-6530-11D2-901F-00C04FB951ED}
  {DEE824EF-729B-4A0E-9C14-B7117D33A817}
) do reg export "HKLM\SYSTEM\CurrentControlSet\Control\DeviceClasses\%%G" "%OUT%\devclass_%%G.reg" /y >nul 2>&1

echo [3/7] Driver class entries (MEDIA, USB)
reg export "HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e96c-e325-11ce-bfc1-08002be10318}" "%OUT%\class_media.reg" /y >nul
reg export "HKLM\SYSTEM\CurrentControlSet\Control\Class\{36fc9e60-c465-11cf-8056-444553540000}" "%OUT%\class_usb.reg" /y >nul

echo [4/7] ASIO drivers
reg export "HKLM\SOFTWARE\ASIO" "%OUT%\asio.reg" /y >nul 2>&1

echo [5/7] PnP devices with relations
pnputil /enum-devices /connected /relations > "%OUT%\pnputil_devices.txt" 2>&1
pnputil /enum-interfaces > "%OUT%\pnputil_interfaces.txt" 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-PnpDevice | Where-Object { $_.InstanceId -match 'VID_06F8|Inpulse|Hercules' -or $_.FriendlyName -match 'Inpulse|Hercules' } | ForEach-Object { '==== ' + $_.InstanceId + ' | ' + $_.FriendlyName + ' | ' + $_.Class + ' | ' + $_.Status; Get-PnpDeviceProperty -InstanceId $_.InstanceId | Format-Table KeyName,Data -AutoSize -Wrap | Out-String -Width 400 }" > "%OUT%\pnp_properties.txt" 2>&1

echo [6/7] MIDI interface paths as Serato sees them
midi_iface_probe.exe > "%OUT%\midi_iface_probe.txt" 2>&1

echo [7/7] Serato logs (if any)
if exist "%USERPROFILE%\Music\_Serato_\Logs" xcopy /e /i /y /q "%USERPROFILE%\Music\_Serato_\Logs" "%OUT%\serato_logs" >nul

echo.
echo Done. Results are in: %OUT%
pause
