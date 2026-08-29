Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class CH {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string t);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    public static IntPtr Glass = IntPtr.Zero;
    public static IntPtr Parent = IntPtr.Zero;
    public static bool Cb(IntPtr h, IntPtr l) {
        IntPtr f = FindWindowEx(h, IntPtr.Zero, "LumenBoxGlass", null);
        if (f != IntPtr.Zero) { Glass = f; Parent = h; return false; }
        return true;
    }
}
'@
[CH]::EnumWindows([CH+EnumProc]{ param($h, $l) [CH]::Cb($h, $l) }, [IntPtr]::Zero) | Out-Null
if ([CH]::Glass -eq [IntPtr]::Zero) { Write-Host "window not found"; exit 1 }
$defview = [CH]::FindWindowEx([CH]::Parent, [IntPtr]::Zero, "SHELLDLL_DefView", [NullString]::Value)
$sb = New-Object System.Text.StringBuilder 64
[void][CH]::GetClassName([CH]::Parent, $sb, 64)
Write-Host ("parent=0x{0:X} class={1} hosts_DefView={2}" -f [CH]::Parent.ToInt64(), $sb.ToString(), ($defview -ne [IntPtr]::Zero))
# z-order: glass 之上是否还有兄弟窗口
$next = [CH]::FindWindowEx([CH]::Parent, [CH]::Glass, [NullString]::Value, [NullString]::Value)
Write-Host ("sibling_above_glass_exists=" + ($next -ne [IntPtr]::Zero))
