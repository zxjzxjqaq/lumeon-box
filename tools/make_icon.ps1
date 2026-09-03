# tools/make_icon.ps1
# 生成 LumenBox 应用图标：多尺寸 .ico（PNG 压缩条目，Win Vista+ 支持）。
# 主题：两个玻璃容器块拼成字母 L，蓝->紫渐变 + 顶部玻璃高光 + 青色光晕。
# 用法: pwsh -File tools/make_icon.ps1
param(
    [string]$OutDir = (Join-Path (Split-Path $PSScriptRoot -Parent) "assets"),
    [int]$MasterSize = 512
)
Add-Type -AssemblyName System.Drawing

function RR([float]$x,[float]$y,[float]$w,[float]$h,[float]$r){
    $p=New-Object System.Drawing.Drawing2D.GraphicsPath
    $d=$r*2
    $p.AddArc($x,$y,$d,$d,180,90); $p.AddArc($x+$w-$d,$y,$d,$d,270,90)
    $p.AddArc($x+$w-$d,$y+$h-$d,$d,$d,0,90); $p.AddArc($x,$y+$h-$d,$d,$d,90,90)
    $p.CloseFigure(); return $p
}

function DrawMaster([int]$size){
    $bmp=New-Object System.Drawing.Bitmap($size,$size)
    $g=[System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode=[System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::FromArgb(0,0,0,0))

    $panel=RR 20 20 472 472 120
    $pb=New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        [System.Drawing.PointF]::new([single]0,[single]0),[System.Drawing.PointF]::new([single]$size,[single]$size),
        [System.Drawing.Color]::FromArgb(255,23,28,56),[System.Drawing.Color]::FromArgb(255,42,50,92))
    $g.FillPath($pb,$panel)

    $vx=[float]135; $vy=[float]115; $vw=[float]100; $vh=[float]305
    $hx=[float]135; $hy=[float]320; $hw=[float]235; $hh=[float]100

    $sh=RR 150 195 210 290 40
    $shb=New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(70,0,0,10))
    $g.FillPath($shb,$sh)

    $glow=RR 110 95 300 340 95
    $glo=New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(46,70,130,255))
    $g.FillPath($glo,$glow)

    $grad=New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        [System.Drawing.PointF]::new([single]$vx,[single]$vy),
        [System.Drawing.PointF]::new([single]($hx+$hw),[single]($hy+$hh)),
        [System.Drawing.Color]::FromArgb(255,64,124,230),[System.Drawing.Color]::FromArgb(255,124,92,238))
    $vb=RR $vx $vy $vw $vh 40
    $hb=RR $hx $hy $hw $hh 40
    $g.FillPath($grad,$vb)
    $g.FillPath($grad,$hb)

    function Sheen($path,$bx,$by,$bw,$bh){
        $s=$g.Save(); $g.SetClip($path)
        $sheen=New-Object System.Drawing.Drawing2D.LinearGradientBrush(
            [System.Drawing.PointF]::new([single]$bx,[single]$by),
            [System.Drawing.PointF]::new([single]$bx,[single]($by+$bh*0.5)),
            [System.Drawing.Color]::FromArgb(78,255,255,255),[System.Drawing.Color]::FromArgb(0,255,255,255))
        $g.FillRectangle($sheen,$bx,$by,$bw,$bh)
        $g.Restore($s)
    }
    Sheen $vb $vx $vy $vw $vh
    Sheen $hb $hx $hy $hw $hh

    $border=New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(110,255,255,255),3)
    $g.DrawPath($border,$vb)
    $g.DrawPath($border,$hb)

    $lit=RR ($vx+10) ($vy+12) ($vw-20) 120 24
    $litb=New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        [System.Drawing.PointF]::new([single]$vx,[single]$vy),
        [System.Drawing.PointF]::new([single]($vx+$vw),[single]($vy+140)),
        [System.Drawing.Color]::FromArgb(70,150,225,255),[System.Drawing.Color]::FromArgb(0,150,225,255))
    $g.FillPath($litb,$lit)

    $g.Dispose()
    return $bmp
}

# 渲染主图（也单独存一张 512 PNG 供预览/文档）
if(-not (Test-Path $OutDir)){ New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }
$master = DrawMaster $MasterSize
$master.Save((Join-Path $OutDir "icon_512.png"),[System.Drawing.Imaging.ImageFormat]::Png)

# 多尺寸 .ico
$sizes = @(16,24,32,48,64,128,256)
$images = @()
foreach($s in $sizes){
    $b=New-Object System.Drawing.Bitmap($s,$s)
    $g=[System.Drawing.Graphics]::FromImage($b)
    $g.InterpolationMode=[System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode=[System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.CompositingQuality=[System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $g.Clear([System.Drawing.Color]::FromArgb(0,0,0,0))
    $g.DrawImage($master,0,0,$s,$s)
    $g.Dispose()
    $ms=New-Object System.IO.MemoryStream
    $b.Save($ms,[System.Drawing.Imaging.ImageFormat]::Png)
    $images += ,@($s,$ms.ToArray())
    $ms.Dispose(); $b.Dispose()
}
$icoPath = Join-Path $OutDir "icon.ico"
$fs=[System.IO.File]::Create($icoPath)
$bw=New-Object System.IO.BinaryWriter($fs)
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$images.Count)
$offset = 6 + 16*$images.Count
foreach($img in $images){
    $s=$img[0]; $bytes=$img[1]
    $dim = if($s -ge 256){ 0 } else { $s }
    $bw.Write([Byte]$dim); $bw.Write([Byte]$dim)
    $bw.Write([Byte]0); $bw.Write([Byte]0)
    $bw.Write([UInt16]1); $bw.Write([UInt16]32)
    $bw.Write([UInt32]$bytes.Length); $bw.Write([UInt32]$offset)
    $offset += $bytes.Length
}
foreach($img in $images){ $bw.Write([Byte[]]$img[1]) }
$bw.Flush(); $bw.Close(); $fs.Close()
$master.Dispose()
Write-Output ("OK  -> {0}  (sizes: {1})" -f $icoPath, ($sizes -join ','))