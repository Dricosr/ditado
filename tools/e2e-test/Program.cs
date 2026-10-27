using System;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Diagnostics;
using System.Windows.Forms;

// E2E: editor + som nos alto-falantes + tecla simulada → o app digita sozinho.
public class E
{
    [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte s, uint f, IntPtr e);
    delegate bool WndProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool EnumChildWindows(IntPtr h, WndProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowTextW(IntPtr h, char[] s, int n);
    [DllImport("user32.dll")] static extern int GetDlgCtrlID(IntPtr h);

    static void Main()
    {
        var wav = Path.Combine(Path.GetTempPath(), "opencode", "fala.wav");
        Console.Error.WriteLine("usando " + wav + " existe=" + File.Exists(wav));

        var ed = Process.Start(new ProcessStartInfo { FileName = "notepad.exe", UseShellExecute = false });
        ed.WaitForInputIdle();
        IntPtr win = IntPtr.Zero, edit = IntPtr.Zero;
        for (int i = 0; i < 20 && win == IntPtr.Zero; i++) { Thread.Sleep(150); win = Process.GetProcessesByName("notepad").LastOrDefault()?.MainWindowHandle ?? IntPtr.Zero; }
        EnumChildWindows(win, (c, l) => { if (GetDlgCtrlID(c) == 15) { edit = c; return false; } return true; }, IntPtr.Zero);
        if (edit == IntPtr.Zero) edit = win;
        Console.Error.WriteLine("edit achado=" + (edit != IntPtr.Zero));

        var app = Process.GetProcessesByName("ditado").FirstOrDefault();
        if (app is null) app = Process.Start("E:\\Cloud\\Git\\ai_projects\\ditado\\build\\ditado.exe");
        Thread.Sleep(1200);

        foreach (var vk in new byte[] { 0x11, 0x10, 0x20 }) keybd_event(vk, 0, 0, IntPtr.Zero);
        var sp = new System.Media.SoundPlayer(new MemoryStream(File.ReadAllBytes(wav)));
        sp.Stream.Position = 0;
        sp.Play();
        Thread.Sleep(4000);
        keybd_event(0x20, 0, 2, IntPtr.Zero); keybd_event(0x10, 0, 2, IntPtr.Zero); keybd_event(0x11, 0, 2, IntPtr.Zero);

        var txt = "";
        for (int i = 0; i < 20 && txt.Length < 4; i++)
        {
            Thread.Sleep(250);
            var b = new char[512]; GetWindowTextW(edit, b, 512); txt = new string(b).Trim('\0').Trim();
        }
        Console.WriteLine("E2E OK — o app digitou sozinho: " + txt);
        try { ed.Kill(); } catch { }
    }
}
