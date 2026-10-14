# Second capture: Hercules driver registration, ASIO driver details and
# installed Hercules software that Serato DJ checks before activating the
# controller. Run elevated (capture2.bat does this) with the controller plugged in.
$ErrorActionPreference = 'SilentlyContinue'
$out = Join-Path $PSScriptRoot 'output2'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$pattern = 'Hercules|Guillemot|DJControl|HDJ|DJHERCULESMIX'

function Export-Key($key, $name) {
    reg export $key (Join-Path $out "$name.reg") /y *> $null
}

Write-Host '[1/7] Hercules / Guillemot registry keys'
Export-Key 'HKLM\SOFTWARE\WOW6432Node\Guillemot Corporation' 'guillemot_wow64'
Export-Key 'HKLM\SOFTWARE\Guillemot Corporation' 'guillemot'
Export-Key 'HKCU\SOFTWARE\Guillemot Corporation' 'guillemot_hkcu'
Export-Key 'HKLM\SOFTWARE\WOW6432Node\Hercules' 'hercules_wow64'
Export-Key 'HKLM\SOFTWARE\Hercules' 'hercules'
Export-Key 'HKCU\SOFTWARE\Hercules' 'hercules_hkcu'

Write-Host '[2/7] ASIO drivers and their COM registration'
Export-Key 'HKLM\SOFTWARE\ASIO' 'asio'
Export-Key 'HKLM\SOFTWARE\WOW6432Node\ASIO' 'asio_wow64'
& {
    foreach ($view in 'SOFTWARE\ASIO', 'SOFTWARE\WOW6432Node\ASIO') {
        foreach ($drv in Get-ChildItem "HKLM:\$view") {
            $clsid = (Get-ItemProperty $drv.PSPath).CLSID
            "==== $view\$($drv.PSChildName)  CLSID=$clsid"
            foreach ($root in 'SOFTWARE\Classes\CLSID', 'SOFTWARE\Classes\WOW6432Node\CLSID') {
                $key = "HKLM:\$root\$clsid"
                if (-not (Test-Path $key)) { continue }
                foreach ($k in @(Get-Item $key) + @(Get-ChildItem $key -Recurse)) {
                    "  [$($k.Name)]"
                    (Get-ItemProperty $k.PSPath | Select-Object * -ExcludeProperty PS* | Format-List | Out-String).Trim()
                }
            }
        }
    }
} > (Join-Path $out 'asio_clsids.txt')

Write-Host '[3/7] Drivers, services and processes'
Export-Key 'HKLM\SYSTEM\CurrentControlSet\Services\HDJusbaudio' 'service_HDJusbaudio'
Export-Key 'HKLM\SYSTEM\CurrentControlSet\Services\HDJusbaudioks' 'service_HDJusbaudioks'
& {
    '==== Drivers'
    Get-CimInstance Win32_SystemDriver | Where-Object { $_.Name + $_.PathName + $_.DisplayName -match $pattern } |
        Format-List Name, DisplayName, State, StartMode, PathName | Out-String
    '==== Services'
    Get-CimInstance Win32_Service | Where-Object { $_.Name + $_.PathName + $_.DisplayName -match $pattern } |
        Format-List Name, DisplayName, State, StartMode, PathName | Out-String
    '==== Processes'
    Get-Process | Where-Object { $_.Path -match $pattern } | Format-List Name, Id, Path | Out-String
    '==== Startup entries'
    foreach ($k in 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run',
                   'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Run',
                   'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run') {
        Get-ItemProperty $k | Select-Object * -ExcludeProperty PS* | Format-List | Out-String
    }
} > (Join-Path $out 'drivers_services.txt')

Write-Host '[4/7] Installed software entries'
Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
                 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*' |
    Where-Object { $_.DisplayName -match "$pattern|Serato" } |
    Format-List DisplayName, DisplayVersion, Publisher, InstallLocation, UninstallString, PSPath |
    Out-File (Join-Path $out 'uninstall.txt')

Write-Host '[5/7] Hercules files with versions'
$roots = 'C:\Program Files', 'C:\Program Files (x86)', 'C:\ProgramData'
& {
    foreach ($r in $roots) {
        Get-ChildItem $r -Directory | Where-Object { $_.Name -match $pattern } |
            Get-ChildItem -Recurse -File
    }
    Get-ChildItem 'C:\Windows\System32', 'C:\Windows\System32\drivers', 'C:\Windows\SysWOW64' -File |
        Where-Object { $_.Name -match 'HDJ|Hercules|DJHERCULES' }
} | ForEach-Object {
    '{0} | {1} | {2}' -f $_.FullName, $_.Length, $_.VersionInfo.FileVersion
} | Out-File (Join-Path $out 'files.txt')

Write-Host '[6/7] Serato settings keys'
Export-Key 'HKCU\SOFTWARE\Serato' 'serato_hkcu'

Write-Host '[7/7] Serato logs'
$logs = Join-Path $env:USERPROFILE 'Music\_Serato_\Logs'
if (Test-Path $logs) { Copy-Item $logs (Join-Path $out 'serato_logs') -Recurse -Force }

Write-Host ''
Write-Host "Done. Results are in: $out"
