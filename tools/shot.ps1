param([string]$out)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;using System.Runtime.InteropServices;
public class W{[StructLayout(LayoutKind.Sequential)]public struct R{public int l,t,r,b;}
[DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out R r);
[DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int c);
[DllImport("user32.dll")]public static extern bool SetProcessDPIAware();}
"@
[W]::SetProcessDPIAware() | Out-Null
$p = Get-Process pcsx2-qt -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { Write-Output "no window"; exit 1 }
Write-Output ("title: " + $p.MainWindowTitle)
[W]::ShowWindow($p.MainWindowHandle, 9) | Out-Null
[W]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 400
$r = New-Object W+R; [W]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
$w=$r.r-$r.l; $h=$r.b-$r.t; Write-Output "$w x $h"
$b = New-Object System.Drawing.Bitmap $w,$h
$g = [System.Drawing.Graphics]::FromImage($b)
$g.CopyFromScreen($r.l,$r.t,0,0,(New-Object System.Drawing.Size $w,$h))
$b.Save($out); $g.Dispose(); $b.Dispose()
