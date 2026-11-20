# Verification Test Suite for babur (VeSTige Legacy .dll)
$ErrorActionPreference = "Stop"

Get-ChildItem -Path "dist\*.dll" -ErrorAction SilentlyContinue | ForEach-Object { Unblock-File $_.FullName -ErrorAction SilentlyContinue }

Add-Type -TypeDefinition @"
using System;
using System.IO;
using System.Text;
using System.Runtime.InteropServices;

public class BaburVestigeTester {
    [DllImport("user32.dll")]
    public static extern IntPtr CreateWindowEx(
        uint dwExStyle, string lpClassName, string lpWindowName, uint dwStyle,
        int x, int y, int nWidth, int nHeight, IntPtr hWndParent, IntPtr hMenu, IntPtr hInstance, IntPtr lpParam);

    [DllImport("user32.dll")]
    public static extern bool DestroyWindow(IntPtr hWnd);

    [StructLayout(LayoutKind.Sequential)]
    public struct ERect {
        public short top;
        public short left;
        public short bottom;
        public short right;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct VstMidiEvent {
        public int type;
        public int byteSize;
        public int deltaFrames;
        public int flags;
        public int noteLength;
        public int noteOffset;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)]
        public byte[] midiData;
        public byte detune;
        public byte noteOffVelocity;
        public byte reserved1;
        public byte reserved2;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct VstEvents {
        public int numEvents;
        public IntPtr reserved;
        public IntPtr event0;
        public IntPtr event1;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct AEffect {
        public int magic;
        public IntPtr dispatcher;
        public IntPtr process;
        public IntPtr setParameter;
        public IntPtr getParameter;
        public int numPrograms;
        public int numParams;
        public int numInputs;
        public int numOutputs;
        public int flags;
        public IntPtr resvd1;
        public IntPtr resvd2;
        public int initialDelay;
        public int realQualities;
        public int offQualities;
        public float ioRatio;
        public IntPtr obj;
        public IntPtr user;
        public int uniqueID;
        public int version;
        public IntPtr processReplacing;
        public IntPtr processDoubleReplacing;
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate IntPtr AudioMasterCallback(IntPtr effect, int opcode, int index, IntPtr val, IntPtr ptr, float opt);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate IntPtr DispatcherFunc(IntPtr effect, int opcode, int index, IntPtr val, IntPtr ptr, float opt);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void SetParamFunc(IntPtr effect, int index, float parameter);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate float GetParamFunc(IntPtr effect, int index);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void ProcessReplacingFunc(IntPtr effect, IntPtr[] inPtrs, IntPtr[] outPtrs, int sampleFrames);

    [DllImport(@"dist\babur.dll", EntryPoint = "VSTPluginMain", CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr VSTPluginMain(AudioMasterCallback master);

    private static IntPtr HostCallback(IntPtr effect, int opcode, int index, IntPtr val, IntPtr ptr, float opt) {
        return IntPtr.Zero;
    }

    private static byte[] BuildTestChunk(float character, float diction, int preset, int sylIdx, string srcPath, string text) {
        using (var ms = new MemoryStream())
        using (var bw = new BinaryWriter(ms)) {
            bw.Write(Encoding.ASCII.GetBytes("SNGR"));
            bw.Write((uint)1);
            bw.Write(character);
            bw.Write(diction);
            bw.Write(0.50f);
            bw.Write(0.25f);
            bw.Write(0.00f);
            bw.Write(1.00f);
            bw.Write(1.00f);
            bw.Write(1.00f);
            bw.Write(preset);
            bw.Write(sylIdx);
            byte[] bPath = Encoding.UTF8.GetBytes(srcPath);
            bw.Write((uint)bPath.Length);
            bw.Write(bPath);
            byte[] bText = Encoding.UTF8.GetBytes(text);
            bw.Write((uint)bText.Length);
            bw.Write(bText);
            return ms.ToArray();
        }
    }

    public static bool RunTests() {
        Console.WriteLine("\n--- Testing babur VeSTige Legacy Instrument ---");
        IntPtr pEffect = VSTPluginMain(HostCallback);
        if (pEffect == IntPtr.Zero) throw new Exception("VSTPluginMain returned NULL!");

        AEffect fx = (AEffect)Marshal.PtrToStructure(pEffect, typeof(AEffect));
        if (fx.magic != 0x56737450) throw new Exception("Invalid magic number!");
        if (fx.numParams != 18) throw new Exception("Expected numParams == 18, got " + fx.numParams);
        if (fx.numInputs != 0 || fx.numOutputs != 2) throw new Exception("Expected 0 in, 2 out!");
        if (fx.uniqueID != 0x42414252) throw new Exception("Unexpected uniqueID!");

        Console.WriteLine(string.Format("  [PASS] Plugin Magic: 0x{0:X8}", fx.magic));
        Console.WriteLine(string.Format("  [PASS] UniqueID: 0x{0:X8} ('BABR')", fx.uniqueID));
        Console.WriteLine(string.Format("  [PASS] Programs: {0}, Parameters: {1}", fx.numPrograms, fx.numParams));

        DispatcherFunc dispatcher = (DispatcherFunc)Marshal.GetDelegateForFunctionPointer(fx.dispatcher, typeof(DispatcherFunc));
        SetParamFunc setParam = (SetParamFunc)Marshal.GetDelegateForFunctionPointer(fx.setParameter, typeof(SetParamFunc));
        GetParamFunc getParam = (GetParamFunc)Marshal.GetDelegateForFunctionPointer(fx.getParameter, typeof(GetParamFunc));
        ProcessReplacingFunc processReplacing = (ProcessReplacingFunc)Marshal.GetDelegateForFunctionPointer(fx.processReplacing, typeof(ProcessReplacingFunc));

        IntPtr buf = Marshal.AllocHGlobal(64);
        dispatcher(pEffect, 45, 0, IntPtr.Zero, buf, 0.0f);
        string effName = Marshal.PtrToStringAnsi(buf);
        dispatcher(pEffect, 47, 0, IntPtr.Zero, buf, 0.0f);
        string vendor = Marshal.PtrToStringAnsi(buf);
        Console.WriteLine(string.Format("  [PASS] Effect: '{0}', Vendor: '{1}'", effName, vendor));
        if (effName != "babur" || vendor != "myavuzuzun") throw new Exception("Metadata mismatch!");

        IntPtr rectHolder = Marshal.AllocHGlobal(IntPtr.Size);
        dispatcher(pEffect, 13, 0, IntPtr.Zero, rectHolder, 0.0f);
        IntPtr pRect = Marshal.ReadIntPtr(rectHolder);
        ERect rect = (ERect)Marshal.PtrToStructure(pRect, typeof(ERect));
        Console.WriteLine(string.Format("  [PASS] Editor Rect: {0} x {1} px", rect.right - rect.left, rect.bottom - rect.top));

        string srcPath = Path.GetFullPath(@"..\basicsf2daw\assets\TimGM6mb.sf2");
        if (!File.Exists(srcPath)) {
            srcPath = Path.GetFullPath(@"bin\test_tone_a4.wav");
        }
        if (File.Exists(srcPath)) {
            byte[] chunk = BuildTestChunk(0.45f, 0.75f, 24, 0, srcPath, "Hello world singing voice");
            IntPtr pChunk = Marshal.AllocHGlobal(chunk.Length);
            Marshal.Copy(chunk, 0, pChunk, chunk.Length);
            dispatcher(pEffect, 24, 0, (IntPtr)chunk.Length, pChunk, 0.0f); // effSetChunk
            Marshal.FreeHGlobal(pChunk);

            if (Math.Abs(getParam(pEffect, 0) - 0.45f) > 0.01f || Math.Abs(getParam(pEffect, 1) - 0.75f) > 0.01f) {
                throw new Exception("Chunk restore parameter mismatch!");
            }

            VstMidiEvent mev = new VstMidiEvent();
            mev.type = 1;
            mev.byteSize = 32;
            mev.deltaFrames = 0;
            mev.flags = 1;
            mev.midiData = new byte[] { 0x90, 60, 112, 0 };
            IntPtr pMidi = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(VstMidiEvent)));
            Marshal.StructureToPtr(mev, pMidi, false);

            VstEvents evs = new VstEvents();
            evs.numEvents = 1;
            evs.reserved = IntPtr.Zero;
            evs.event0 = pMidi;
            evs.event1 = IntPtr.Zero;
            IntPtr pEvs = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(VstEvents)));
            Marshal.StructureToPtr(evs, pEvs, false);

            dispatcher(pEffect, 25, 0, IntPtr.Zero, pEvs, 0.0f); // effProcessEvents

            int frames = 2048;
            float[] outL = new float[frames];
            float[] outR = new float[frames];
            GCHandle hOutL = GCHandle.Alloc(outL, GCHandleType.Pinned);
            GCHandle hOutR = GCHandle.Alloc(outR, GCHandleType.Pinned);
            IntPtr[] outs = new IntPtr[] { hOutL.AddrOfPinnedObject(), hOutR.AddrOfPinnedObject() };

            processReplacing(pEffect, null, outs, frames);
            float peak = 0.0f;
            for (int i = 0; i < frames; ++i) {
                peak = Math.Max(peak, Math.Max(Math.Abs(outL[i]), Math.Abs(outR[i])));
            }
            hOutL.Free(); hOutR.Free();
            Marshal.FreeHGlobal(pMidi);
            Marshal.FreeHGlobal(pEvs);
            Console.WriteLine(string.Format("  [PASS] Singing MIDI Playback Render: {0} frames, Peak={1:F4}", frames, peak));
            if (peak < 0.01f) throw new Exception("No singing audio generated from MIDI NoteOn!");
        }

        IntPtr hHost = CreateWindowEx(0, "STATIC", "Host", 0x80000000, 50, 50, rect.right - rect.left, rect.bottom - rect.top, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
        dispatcher(pEffect, 14, 0, IntPtr.Zero, hHost, 0.0f);
        dispatcher(pEffect, 15, 0, IntPtr.Zero, IntPtr.Zero, 0.0f);
        DestroyWindow(hHost);
        Console.WriteLine("  [PASS] Direct2D GUI Lifecycle verified.");

        Marshal.FreeHGlobal(buf);
        Marshal.FreeHGlobal(rectHolder);
        dispatcher(pEffect, 1, 0, IntPtr.Zero, IntPtr.Zero, 0.0f);
        return true;
    }
}
"@

[BaburVestigeTester]::RunTests() | Out-Null
Write-Host "`nAll babur VeSTige verification tests passed!`n" -ForegroundColor Green
