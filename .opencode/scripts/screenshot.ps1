# 截图脚本：启动/定位 translex 主窗口并截图保存（review agent 视觉验证用）
# 用法：pwsh -File .opencode/scripts/screenshot.ps1 [-Out <png路径>] [-WaitMs <毫秒>]
param(
    [string]$Out = (Join-Path $env:TEMP "translex_shot.png"),
    [int]$WaitMs = 3500
)

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class TranslexCap {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out int l, out int t, out int r, out int b);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
}
"@

$appDir = Join-Path $PSScriptRoot "..\.."
$exe = Join-Path $appDir "build-vs2026-x64\Debug\translex.exe"

# 1) 若应用未运行则启动
$proc = Get-Process -Name translex -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) {
    if (Test-Path $exe) {
        $env:PATH = "D:/Software/Qt/6.5.3/msvc2019_64/bin;" + $env:PATH
        Start-Process -FilePath $exe | Out-Null
        $proc = Get-Process -Name translex -ErrorAction SilentlyContinue | Select-Object -First 1
    }
}
if (-not $proc) {
    Write-Error "translex 未运行且启动失败（exe: $exe）"
    exit 1
}
Start-Sleep -Milliseconds $WaitMs

# 2) 定位主窗口（取面积最大的顶层窗口）
$hWnd = [IntPtr]::Zero
$bestArea = 0
$winLeft = 0; $winTop = 0; $winW = 0; $winH = 0
foreach ($h in @($proc.MainWindowHandle)) {
    if ($h -eq [IntPtr]::Zero) { continue }
    $l = 0; $t = 0; $rr = 0; $bb = 0
    $got = [TranslexCap]::GetWindowRect([IntPtr]$h, [ref]$l, [ref]$t, [ref]$rr, [ref]$bb)
    if ($got) {
        $w = $rr - $l
        $hgt = $bb - $t
        $area = $w * $hgt
        if ($area -gt $bestArea) {
            $bestArea = $area
            $hWnd = [IntPtr]$h
            $winLeft = $l; $winTop = $t; $winW = $w; $winH = $hgt
        }
    }
}

$bmp = $null
if ($hWnd -ne [IntPtr]::Zero -and $bestArea -gt 0) {
    # 优先 PrintWindow（无需窗口前置/遮挡也能捕获）
    $bmp = New-Object System.Drawing.Bitmap($winW, $winH)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    $ok = [TranslexCap]::PrintWindow($hWnd, $hdc, 0)
    $g.ReleaseHdc($hdc)
    $g.Dispose()
    if (-not $ok) {
        $bmp.Dispose()
        $bmp = $null
    }
}
if (-not $bmp) {
    # 兜底：窗口区域 CopyFromScreen（窗口需在屏幕内）→ 再退全屏
    Write-Warning "PrintWindow 失败，改用 CopyFromScreen"
    if ($bestArea -gt 0) {
        $bounds = New-Object System.Drawing.Rectangle($winLeft, $winTop, $winW, $winH)
    } else {
        $bounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
    }
    $bmp = New-Object System.Drawing.Bitmap([int]$bounds.Width, [int]$bounds.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen([System.Drawing.Point]$bounds.Location,
                      [System.Drawing.Point]::Empty, $bounds.Size)
    $g.Dispose()
}

$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output $Out
