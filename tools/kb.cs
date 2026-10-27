using System;
using System.Runtime.InteropServices;

public class KBZ
{
    [StructLayout(LayoutKind.Sequential)] struct KB2 { public ushort vk; public ushort scan; public uint flags; public uint time; public UIntPtr extra; public ulong trans; }
    [StructLayout(LayoutKind.Sequential)] struct INP { public uint type; public uint pad; public KB2 k; }
    [DllImport("user32.dll", SetLastError = true)] static extern uint SendInput(uint n, INP[] a, int size);

    public static void Segura() { Do(true); }
    public static void Solta() { Do(false); }
    static void Do(bool down)
    {
        INP[] a = new INP[] {
            mk(0x11, down), mk(0x10, down), mk(0x20, down),
        };
        uint n = SendInput(3, a, Marshal.SizeOf(typeof(INP)));
        if (n != 3) throw new Exception("sendinput=" + n + " err=" + Marshal.GetLastWin32Error());
    }
    static INP mk(ushort vk, bool down)
    {
        INP i = new INP();
        i.type = 1;
        i.k.vk = vk;
        i.k.flags = down ? 0u : 2u;
        return i;
    }
}
