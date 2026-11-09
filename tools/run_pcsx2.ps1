# Boots the game fresh (used by tools/make_state.py).
# Where the game disc image and PCSX2 are. Set them once, for example in your PowerShell profile:
#   $env:GTASA_ISO = 'D:\isos\GTA San Andreas (Germany) (En,De) (v1.00).iso'   (SLES-52927, CRC A3EF1321)
#   $env:PCSX2_EXE = 'C:\pcsx2\pcsx2-qt.exe'
$iso = $env:GTASA_ISO
$exe = $env:PCSX2_EXE
if (-not $iso -or -not (Test-Path $iso)) { Write-Error 'Set GTASA_ISO to your SLES-52927 disc image.'; exit 1 }
if (-not $exe -or -not (Test-Path $exe)) { Write-Error 'Set PCSX2_EXE to pcsx2-qt.exe.'; exit 1 }
Start-Process -FilePath $exe -ArgumentList @('-fastboot','--',"`"$iso`"") -WorkingDirectory (Split-Path $exe)
