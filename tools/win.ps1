# Focus-free PCSX2 window helpers: -shot FILE, -keys "k,k,Return" (vk via PostMessage to the render child)
param([string]$shot,[string]$keys,[int]$hold=120,[int]$gap=500)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;using System.Text;using System.Collections.Generic;using System.Runtime.InteropServices;
public class W{
[StructLayout(LayoutKind.Sequential)]public struct R{public int l,t,r,b;}
public delegate bool EP(IntPtr h,IntPtr l);
[DllImport("user32.dll")]public static extern bool GetClientRect(IntPtr h,out R r);
[DllImport("user32.dll")]public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint f);
[DllImport("user32.dll")]public static extern bool EnumChildWindows(IntPtr h,EP cb,IntPtr l);
[DllImport("user32.dll")]public static extern int GetClassName(IntPtr h,StringBuilder s,int n);
[DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
[DllImport("user32.dll")]public static extern uint MapVirtualKey(uint c,uint t);
[DllImport("user32.dll")]public static extern bool SetProcessDPIAware();
[DllImport("user32.dll")]public static extern bool IsIconic(IntPtr h);
[DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int c);
[DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr FindWindow(string c,string t);
public static List<IntPtr> Kids(IntPtr h){var l=new List<IntPtr>();EnumChildWindows(h,(x,y)=>{l.Add(x);return true;},IntPtr.Zero);return l;}
public static string Cls(IntPtr h){var s=new StringBuilder(256);GetClassName(h,s,256);return s.ToString();}
}
"@
[W]::SetProcessDPIAware() | Out-Null
$h = [W]::FindWindow([NullString]::Value,'Grand Theft Auto - San Andreas')
if ($h -eq [IntPtr]::Zero) { Write-Output "no window"; exit 1 }
if ([W]::IsIconic($h)) { Write-Output "was minimized, restoring without focus"; [W]::ShowWindow($h,4) | Out-Null; Start-Sleep -Milliseconds 500 }
$kids = [W]::Kids($h)
if ($shot) {
  $r = New-Object W+R; [W]::GetClientRect($h,[ref]$r) | Out-Null
  $b = New-Object System.Drawing.Bitmap ($r.r-$r.l),($r.b-$r.t)
  $g = [System.Drawing.Graphics]::FromImage($b); $dc=$g.GetHdc()
  [W]::PrintWindow($h,$dc,3) | Out-Null
  $g.ReleaseHdc($dc); $b.Save($shot); $g.Dispose(); $b.Dispose()
  Write-Output ("shot ok; kids=" + (($kids | ForEach-Object { [W]::Cls($_) }) -join ','))
}
if ($keys) {
  $map=@{Return=0x0D;Up=0x26;Down=0x28;Left=0x25;Right=0x27;Backspace=0x08;Shift=0x10;Tab=0x09;Space=0x20}
  $targets=@($h)+$kids
  foreach($k in $keys.Split(',')){
    $vk = if($map.ContainsKey($k)){$map[$k]}else{[int][char]$k.ToUpper()}
    $sc=[W]::MapVirtualKey($vk,0)
    $ext= if($vk -ge 0x25 -and $vk -le 0x28){1 -shl 24}else{0}
    $dn=[IntPtr](1 -bor ($sc -shl 16) -bor $ext); $up=[IntPtr]([int64](0xC0000001L -bor ($sc -shl 16) -bor $ext))
    foreach($t in $targets){[W]::PostMessage($t,0x100,[IntPtr]$vk,$dn)|Out-Null}
    Start-Sleep -Milliseconds $hold
    foreach($t in $targets){[W]::PostMessage($t,0x101,[IntPtr]$vk,$up)|Out-Null}
    Start-Sleep -Milliseconds $gap
  }
  Write-Output "keys sent"
}
