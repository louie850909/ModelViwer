using System;
using System.Runtime.InteropServices;

namespace UI;

internal static class RenderBridge
{
    private const string DLL = "Renderer.dll";

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern bool Renderer_Init(IntPtr panelUnknown, int width, int height);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern void Renderer_Resize(int width, int height, float scale);

    // 宣告與 C++ 對應的委派 (Delegate)，必須標註 Cdecl 呼叫慣例
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void LoadCallback();

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern bool Renderer_LoadModel([MarshalAs(UnmanagedType.LPStr)] string path, LoadCallback callback);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern void Renderer_Shutdown();

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern void Renderer_SetCameraTransform(float px, float py, float pz, float pitch, float yaw);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern void Renderer_GetStats(out int vertices, out int polygons, out int drawCalls, out float frameTimeMs);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern int Renderer_GetNodeCount();

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern void Renderer_GetNodeInfo(int index, byte[] outName, int maxLen, out int outParentIndex);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern void Renderer_GetNodeTransform(int index, float[] outT, float[] outR, float[] outS);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern void Renderer_SetNodeTransform(int index, float[] t, float[] r, float[] s);

    /// <summary>
    /// [新增] 批次更新所有 Node 的 Transform，單次 P/Invoke 傳入 pinned float 陣列指標。
    /// 每個 Node 佔 10 個 float：Translation(3) + Rotation(4, quaternion) + Scale(3)。
    /// 請透過 NodeTransformBatcher.FlushToCpp() 呼叫，不要直接使用。
    /// </summary>
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern void Renderer_SetAllNodeTransforms(IntPtr data, int nodeCount);
}
