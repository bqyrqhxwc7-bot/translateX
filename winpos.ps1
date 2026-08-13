Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32W {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    public delegate bool EnumProc(IntPtr h, IntPtr lp);
    public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
$target = (Get-Process -Name translateXqml | Select-Object -First 1).Id
$mainH = [IntPtr]::Zero; $mainR = $null
$cb = {
    param($h, $lp)
    $pid2 = 0
    [Win32W]::GetWindowThreadProcessId($h, [ref]$pid2) | Out-Null
    if ($pid2 -eq $target -and [Win32W]::IsWindowVisible($h)) {
        $r = New-Object Win32W+RECT
        [Win32W]::GetWindowRect($h, [ref]$r) | Out-Null
        $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
        if ($w -gt 800 -and $ht -gt 500) { $script:mainH = $h; $script:mainR = $r }
    }
    return $true
}
[Win32W]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
Write-Host "main at $($mainR.Left),$($mainR.Top) size $($mainR.Right-$mainR.Left)x$($mainR.Bottom-$mainR.Top)"
