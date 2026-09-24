#requires -Version 5.1
# LumenBox 显示/工作区诊断（只读收集，不改任何设置）
# 运行后在桌面生成 LumenBox-Diag-Report.txt，把该文件原样发回即可
param([switch]$NoOpen)

$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch {}

$out = New-Object System.Collections.Generic.List[string]
function Log { param($s) $out.Add([string]$s) | Out-Null }

Add-Type -AssemblyName System.Windows.Forms | Out-Null

Add-Type @"
using System;
using System.Runtime.InteropServices;

public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
public struct POINT { public int X; public int Y; }

[StructLayout(LayoutKind.Sequential)]
public struct MONITORINFO {
    public int cbSize;
    public RECT rcMonitor;
    public RECT rcWork;
    public uint dwFlags;
}

[StructLayout(LayoutKind.Sequential)]
public struct APPBARDATA {
    public int cbSize;
    public IntPtr hWnd;
    public uint uCallbackMessage;
    public uint uEdge;
    public RECT rc;
    public int lParam;
}

public static class LB {
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string title);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SystemParametersInfoW(uint action, uint param, out RECT rect, uint update);
    [DllImport("user32.dll")] public static extern IntPtr MonitorFromWindow(IntPtr hwnd, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr MonitorFromPoint(POINT pt, uint flags);
    [DllImport("user32.dll")] public static extern bool GetMonitorInfoW(IntPtr hmon, ref MONITORINFO mi);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hwnd);
    [DllImport("shcore.dll")] public static extern int GetDpiForMonitor(IntPtr hmon, int type, out uint dpiX, out uint dpiY);
    [DllImport("shell32.dll")] public static extern IntPtr SHAppBarMessage(uint msg, ref APPBARDATA data);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
}
"@

# 与程序相同的 DPI 感知，保证 DPI/坐标读数与程序视角一致
try { [void][LB]::SetProcessDpiAwarenessContext([IntPtr](-4)) } catch {}

function FmtRect { param($r) "({0},{1})-({2},{3})  {4}x{5}" -f $r.Left, $r.Top, $r.Right, $r.Bottom, ($r.Right-$r.Left), ($r.Bottom-$r.Top) }
function Pct { param($dpi) "{0:P0}" -f ($dpi/96.0) }

Log "================ LumenBox 显示/工作区诊断 ================"
Log ("时间: {0}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))

# ---------- 1. 系统 ----------
$os = Get-CimInstance Win32_OperatingSystem
Log ""
Log "---- 系统 ----"
Log ("Windows: {0}  Build {1}  (Win11={2})" -f $os.Caption, $os.BuildNumber, ([int]$os.BuildNumber -ge 22000))

# ---------- 2. 显示器（逐屏 DPI 与工作区） ----------
Log ""
Log "---- 显示器（物理像素 / 逐屏 DPI）----"
$monInfos = @()
$idx = 0
foreach ($s in [System.Windows.Forms.Screen]::AllScreens) {
    $b = $s.Bounds; $w = $s.WorkingArea
    $pt = New-Object POINT; $pt.X = $b.X + [int]($b.Width/2); $pt.Y = $b.Y + [int]($b.Height/2)
    $hmon = [LB]::MonitorFromPoint($pt, 2)   # MONITOR_DEFAULTTONEAREST
    $dx = [uint32]0; $dy = [uint32]0
    $hr = [LB]::GetDpiForMonitor($hmon, 0, [ref]$dx, [ref]$dy)   # MDT_EFFECTIVE_DPI
    if ($hr -ne 0 -or $dx -eq 0) { $dx = [uint32]96 }
    $dpiPct = Pct $dx
    Log ("[{0}] Primary={1}  屏幕={2}" -f $idx, $s.Primary, (FmtRect $b))
    Log ("    工作区={0}" -f (FmtRect $w))
    Log ("    DPI={0} ({1})   设备={2}" -f $dx, $dpiPct, $s.DeviceName)
    $monInfos += [pscustomobject]@{ Idx=$idx; Primary=$s.Primary; Bounds=$b; Work=$w; Dpi=$dx; Dev=$s.DeviceName; HMon=$hmon }
    $idx++
}

# ---------- 3. 程序实际使用的主屏工作区（SPI_GETWORKAREA） ----------
$spiWork = New-Object RECT
[void][LB]::SystemParametersInfoW(0x0030, 0, [ref]$spiWork, 0)   # SPI_GETWORKAREA
Log ""
Log "---- SPI_GETWORKAREA（程序 on_extent_ 实际用它定位）----"
Log ("主屏工作区={0}" -f (FmtRect $spiWork))

# ---------- 4. 任务栏 ----------
Log ""
Log "---- 任务栏（shell appbar API，仅主屏）----"
$abd = New-Object APPBARDATA
$abd.cbSize = [System.Runtime.InteropServices.Marshal]::SizeOf([type][APPBARDATA])
$state = [int64][LB]::SHAppBarMessage(0x0004, [ref]$abd)          # ABM_GETSTATE
$autohide = (($state -band 0x1) -ne 0)
[void][LB]::SHAppBarMessage(0x0005, [ref]$abd)                     # ABM_GETTASKBARPOS
$edgeNames = @('左','上','右','下')
$edge = if ($abd.uEdge -le 3) { $edgeNames[$abd.uEdge] } else { '未知' }
Log ("自动隐藏: {0}" -f ($(if ($autohide) { '是  ←  任务栏滑入时会盖住窗口右/下边缘' } else { '否' })))
Log ("所在边: {0}   任务栏矩形: {1}" -f $edge, (FmtRect $abd.rc))

# ---------- 5. LumenBox 窗口（若在运行） ----------
Log ""
Log "---- LumenBox 窗口 ----"
$hwnd = [LB]::FindWindowW('LumenBoxGlass', $null)
$winRect = $null; $winDpi = [uint32]0; $winMon = $null
if ($hwnd -ne [IntPtr]::Zero) {
    $r = New-Object RECT
    [void][LB]::GetWindowRect($hwnd, [ref]$r)
    $winRect = $r
    $winDpi = [LB]::GetDpiForWindow($hwnd)      # ← 程序 DpiScale 用的就是这个值
    $hmon = [LB]::MonitorFromWindow($hwnd, 2)
    $mi = New-Object MONITORINFO; $mi.cbSize = [System.Runtime.InteropServices.Marshal]::SizeOf([type][MONITORINFO])
    [void][LB]::GetMonitorInfoW($hmon, [ref]$mi)
    $winMon = $mi
    $which = ($monInfos | Where-Object { $_.HMon -eq $hmon } | Select-Object -First 1)
    Log ("运行中  可见={0}" -f ([LB]::IsWindowVisible($hwnd)))
    Log ("窗口矩形={0}" -f (FmtRect $r))
    Log ("窗口 DPI={0} ({1})   [程序按此把内容放大]" -f $winDpi, (Pct $winDpi))
    Log ("所在屏: {0}  屏幕区={1}" -f $(if ($which) { '[' + $which.Idx + ']' } else { '?' }), (FmtRect $mi.rcMonitor))
    Log ("所在屏工作区={0}" -f (FmtRect $mi.rcWork))
} else {
    Log "未检测到运行中的 LumenBox 窗口（未启动或已退出）"
}

# ---------- 6. 配置与盒子内容量 ----------
Log ""
Log "---- LumenBox 配置 (%APPDATA%\LumenBox\config.json) ----"
$cfgDir = Join-Path $env:APPDATA 'LumenBox'
$cfgPath = Join-Path $cfgDir 'config.json'
$cfg = $null
if (Test-Path $cfgPath) {
    $raw = [System.IO.File]::ReadAllText($cfgPath)
    try { $cfg = $raw | ConvertFrom-Json } catch { Log ("config.json 解析失败: " + $_.Exception.Message) }
} else {
    Log "找不到 config.json"
}

$fileCounts = @{}
$tasksCount = 0
$selDate = $null
if ($cfg) {
    Log ("mode={0}  anchored={1}  x={2} y={3} w={4} h={5}  hideIconsMode={6} watcherMode={7}" -f `
        $cfg.mode, $cfg.anchored, $cfg.x, $cfg.y, $cfg.w, $cfg.h, $cfg.hideIconsMode, $cfg.watcherMode)
    $boxes = @($cfg.boxes)
    Log ("盒子数: {0}" -f $boxes.Count)
    foreach ($b in $boxes) {
        $n = 0
        if ($b.type -ne 'tasks' -and $b.folder) {
            $d = Join-Path $cfgDir $b.folder
            if (Test-Path $d) { $n = @(Get-ChildItem $d -Force -ErrorAction SilentlyContinue).Count }
            $fileCounts[$b.folder] = $n
        }
        Log ("  id={0}  type={1}  folder={2}  collapsed={3}  项数={4}  名称={5}" -f $b.id, $b.type, $b.folder, $b.collapsed, $n, $b.name)
    }
    $tp = Join-Path $cfgDir 'tasks.json'
    if (Test-Path $tp) {
        try {
            $tj = [System.IO.File]::ReadAllText($tp) | ConvertFrom-Json
            if ($tj.selDate) { $selDate = $tj.selDate }
            $key = if ($selDate) { $selDate } else { (Get-Date -Format 'yyyy-MM-dd') }
            $prop = $tj.tasks.PSObject.Properties[$key]
            if ($prop) { $tasksCount = @($prop.Value).Count }
            Log ("待办盒({0})条数: {1}" -f $key, $tasksCount)
        } catch { Log ("tasks.json 解析失败: " + $_.Exception.Message) }
    }
}

# ---------- 7. 按程序布局算法推算窗口大小/位置 ----------
Log ""
Log "---- 按程序布局算法推算（kPerRow=4, 盒宽200DIP, 间隙14DIP, 行高22DIP, 最多12行）----"
$kPerRow = 4; $kBoxW = 200.0; $kGap = 14.0
$kHeaderH = 34.0; $kRowH = 22.0; $kPad = 12.0; $kDateH = 26.0; $kMaxRows = 12
$dpiForCalc = if ($winDpi -gt 0) { [double]$winDpi } else {
    $p = $monInfos | Where-Object { $_.Primary } | Select-Object -First 1
    if ($p) { [double]$p.Dpi } else { 96.0 }
}
Log ("推算用 DPI = {0} ({1})  {2}" -f $dpiForCalc, (Pct $dpiForCalc), $(if ($winDpi -gt 0) { '（取自运行中窗口）' } else { '（窗口未运行，取主屏）' }))

$predW = 0; $predH = 0; $predX = 0; $predY = 0
$bboxW = 0.0; $bboxH = 0.0
if ($cfg) {
    $boxes = @($cfg.boxes)
    $n = $boxes.Count
    if ($n -gt 0) {
        $gridCols = [Math]::Min($kPerRow, $n)
        $rows = [int][Math]::Ceiling($n / $kPerRow)
        $colBottom = New-Object 'double[]' $gridCols
        $maxBottom = 0.0
        for ($r = 0; $r -lt $rows; $r++) {
            $start = $r * $kPerRow
            $count = [Math]::Min($kPerRow, $n - $start)
            $baseCol = $gridCols - $count
            for ($k = 0; $k -lt $count; $k++) {
                $i = $start + $k
                $col = $baseCol + $k
                $b = $boxes[$i]
                $rc = 0; $tc = 0
                if ($b.type -eq 'tasks') { $tc = [Math]::Min($tasksCount, $kMaxRows) }
                elseif ($b.folder) { $rc = [Math]::Min([int]$fileCounts[$b.folder], $kMaxRows) }
                if ($b.collapsed) { $h = $kHeaderH }
                elseif ($b.type -eq 'tasks') { $h = $kHeaderH + $kDateH + $kPad*0.4 + ($tc+1)*$kRowH + 4 }
                else { $h = $kHeaderH + $kPad + $rc*$kRowH + 4 }
                $y = $colBottom[$col]
                $bottom = $y + $h
                if ($bottom -gt $maxBottom) { $maxBottom = $bottom }
                $colBottom[$col] = $bottom + $kGap
            }
        }
        $bboxW = $gridCols * $kBoxW + ($gridCols - 1) * $kGap
        $bboxH = $maxBottom
        Log ("内容包围盒: {0} x {1} DIP" -f [int]$bboxW, [int]$bboxH)
        $predW = [int]($bboxW * $dpiForCalc / 96.0) + 2
        $predH = [int]($bboxH * $dpiForCalc / 96.0) + 2
        Log ("按程序算法窗口应为: {0} x {1} px" -f $predW, $predH)

        $p = $monInfos | Where-Object { $_.Primary } | Select-Object -First 1
        $wr = if ($p) { $p.Work } else { $spiWork }
        if ($cfg.anchored) {
            $predX = $wr.Right - $predW - 24
            $predY = $wr.Bottom - $predH - 24
            Log ("锚定=右下角自动（anchored=true）: 预计位置 x={0} y={1}" -f $predX, $predY)
        } else {
            $loX = $wr.Left; $hiX = $wr.Right - $predW - 24
            $loY = $wr.Top;  $hiY = $wr.Bottom - $predH - 24
            $predX = [Math]::Min([Math]::Max([int]$cfg.x, $loX), $hiX)
            $predY = [Math]::Min([Math]::Max([int]$cfg.y, $loY), $hiY)
            Log ("锚定=手动（anchored=false）: 保存位置 x={0} y={1} → 夹紧后 x={2} y={3}" -f $cfg.x, $cfg.y, $predX, $predY)
            if ($hiY -lt $loY) {
                Log ("  ⚠ 内容高度({0}px)超过主屏工作区({1}px)，夹紧上限失效 → 窗口顶部将溢出屏幕" -f $predH, ($wr.Bottom-$wr.Top))
            }
            if ($hiX -lt $loX) {
                Log ("  ⚠ 内容宽度({0}px)超过主屏工作区({1}px)，夹紧上限失效 → 窗口左侧将溢出屏幕" -f $predW, ($wr.Right-$wr.Left))
            }
        }
        Log ("预计窗口矩形: ({0},{1})-({2},{3})" -f $predX, $predY, ($predX+$predW), ($predY+$predH))
        Log ("对比主屏工作区: 宽 {0}/{1} px  高 {2}/{3} px" -f $predW, ($wr.Right-$wr.Left), $predH, ($wr.Bottom-$wr.Top))
        if ($predH -gt ($wr.Bottom - $wr.Top)) {
            Log ("  ⚠ 内容高度超出主屏工作区 {0} px（{1} DIP）→ 窗口会溢出屏幕" -f ($predH - ($wr.Bottom-$wr.Top)), [int](($predH - ($wr.Bottom-$wr.Top)) / $dpiForCalc * 96))
        }
        if ($predW -gt ($wr.Right - $wr.Left)) {
            Log ("  ⚠ 内容宽度超出主屏工作区 {0} px" -f ($predW - ($wr.Right-$wr.Left)))
        }
    }
}

# ---------- 8. 实测窗口 vs 可视区（被遮住的直接证据） ----------
if ($winRect) {
    Log ""
    Log "---- 实测窗口边缘 vs 可视区 ----"
    $miWork = if ($winMon) { $winMon.rcWork } else { $spiWork }
    $mb = if ($winMon) { $winMon.rcMonitor } else { $spiWork }
    $overR_work = $winRect.Right - $miWork.Right
    $overB_work = $winRect.Bottom - $miWork.Bottom
    $overR_mon  = $winRect.Right - $mb.Right
    $overB_mon  = $winRect.Bottom - $mb.Bottom
    Log ("窗口右边缘 vs 所在屏工作区右: 差 {0} px  {1}" -f $overR_work, $(if ($overR_work -gt 0) { '⚠ 超出（右侧被遮）' } else { '（未超出）' }))
    Log ("窗口下边缘 vs 所在屏工作区下: 差 {0} px  {1}" -f $overB_work, $(if ($overB_work -gt 0) { '⚠ 超出（下侧被遮）' } else { '（未超出）' }))
    Log ("窗口右边缘 vs 所在屏屏幕右:   差 {0} px  {1}" -f $overR_mon, $(if ($overR_mon -gt 0) { '⚠ 超出屏幕（彻底出屏）' } else { '（未超出）' }))
    Log ("窗口下边缘 vs 所在屏屏幕下:   差 {0} px  {1}" -f $overB_mon, $(if ($overB_mon -gt 0) { '⚠ 超出屏幕（彻底出屏）' } else { '（未超出）' }))
    Log ("窗口右下角与所在屏工作区右下角留缝: 右 {0} px / 下 {1} px （程序设计值=24）" -f ($miWork.Right - $winRect.Right), ($miWork.Bottom - $winRect.Bottom))
    if ($winDpi -gt 0) {
        $which = $monInfos | Where-Object { $_.HMon -eq ([LB]::MonitorFromWindow($hwnd, 2)) } | Select-Object -First 1
        if ($which -and $which.Dpi -ne $winDpi) {
            Log ("  ⚠ 窗口 DPI({0}) ≠ 所在屏 DPI({1}) → 内容按 {2} 渲染、屏按 {3} 缩放，右/下会被裁" -f $winDpi, $which.Dpi, (Pct $winDpi), (Pct $which.Dpi))
        }
    }
}

# ---------- 9. 安装信息 ----------
Log ""
Log "---- 安装信息 ----"
$paths = @("$env:ProgramFiles\LumenBox\lumenbox.exe", "D:\LumenBox\lumenbox.exe", "$env:LOCALAPPDATA\Programs\LumenBox\lumenbox.exe")
$found = $paths | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $found) {
    $keys = @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
              'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
              'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*')
    foreach ($k in $keys) {
        $item = Get-ItemProperty $k -ErrorAction SilentlyContinue | Where-Object { $_.DisplayName -like '*LumenBox*' } | Select-Object -First 1
        if ($item) { $found = Join-Path $item.InstallLocation 'lumenbox.exe'; break }
    }
}
if ($found -and (Test-Path $found)) {
    $v = (Get-Item $found).VersionInfo
    Log ("exe: {0}" -f $found)
    Log ("版本: {0}   修改时间: {1}   大小: {2} 字节" -f $v.FileVersion, (Get-Item $found).LastWriteTime, (Get-Item $found).Length)
} else {
    Log "未找到 lumenbox.exe（可能安装到其它目录）"
}

# ---------- 10. 结论提示 ----------
Log ""
Log "---- 请回传 ----"
Log "本文件完整发回即可；若当时容器右/下被遮，最好在运行本诊断的同时截图保留现场。"
Log "=========================================================="

# ---------- 写报告 ----------
$desktop = [Environment]::GetFolderPath('Desktop')
$reportPath = Join-Path $desktop 'LumenBox-Diag-Report.txt'
try {
    [System.IO.File]::WriteAllLines($reportPath, $out.ToArray(), (New-Object System.Text.UTF8Encoding $true))
} catch {
    $reportPath = Join-Path $PSScriptRoot 'LumenBox-Diag-Report.txt'
    [System.IO.File]::WriteAllLines($reportPath, $out.ToArray(), (New-Object System.Text.UTF8Encoding $true))
}
Write-Host ("报告已生成: " + $reportPath)
if (-not $NoOpen) { try { Start-Process notepad.exe $reportPath } catch {} }
