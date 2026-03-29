using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace UI;

/// <summary>
/// 將所有 Node 的 TRS 資料打包成單一 pinned float[]，
/// 透過一次 P/Invoke (Renderer_SetAllNodeTransforms) 傳遞給 C++，
/// 避免每個 Node 都發起一次 P/Invoke 的 overhead。
/// 
/// 格式：每個 Node 佔 10 個 float
///   [0..2] Translation (x, y, z)
///   [3..6] Rotation    (x, y, z, w)  — quaternion
///   [7..9] Scale       (x, y, z)
/// 
/// 生命週期：隨 MainWindow 建立並 Dispose。
/// </summary>
internal sealed class NodeTransformBatcher : IDisposable
{
    private const int Stride = 10; // floats per node

    private float[]  _buffer  = Array.Empty<float>();
    private GCHandle _handle;
    private IntPtr   _ptr     = IntPtr.Zero;
    private bool     _disposed = false;

    /// <summary>
    /// 將 nodes 列表的 TRS 寫入 pinned buffer，並以單次 P/Invoke 刷入 C++。
    /// 每幀在 UI thread 或 render 準備階段呼叫一次即可。
    /// </summary>
    public void FlushToCpp(IList<NodeViewModel> nodes)
    {
        if (_disposed) return;

        int nodeCount = nodes.Count;
        if (nodeCount == 0) return;

        // 若 node 數量改變（載入新模型），重新分配並 pin
        EnsureBuffer(nodeCount);

        // 打包所有 Node 的 TRS 資料到連續記憶體
        for (int i = 0; i < nodeCount; i++)
        {
            int b = i * Stride;
            NodeViewModel n = nodes[i];

            _buffer[b + 0] = n.TranslationX;
            _buffer[b + 1] = n.TranslationY;
            _buffer[b + 2] = n.TranslationZ;

            _buffer[b + 3] = n.RotationX;
            _buffer[b + 4] = n.RotationY;
            _buffer[b + 5] = n.RotationZ;
            _buffer[b + 6] = n.RotationW;

            _buffer[b + 7] = n.ScaleX;
            _buffer[b + 8] = n.ScaleY;
            _buffer[b + 9] = n.ScaleZ;
        }

        // 單次 P/Invoke：傳 pinned 指標，C++ 直接讀取，零拷貝
        RenderBridge.Renderer_SetAllNodeTransforms(_ptr, nodeCount);
    }

    private void EnsureBuffer(int nodeCount)
    {
        int required = nodeCount * Stride;
        if (_buffer.Length >= required) return;

        // 釋放舊的 pin
        if (_handle.IsAllocated)
            _handle.Free();

        _buffer = new float[required];
        _handle = GCHandle.Alloc(_buffer, GCHandleType.Pinned);
        _ptr    = _handle.AddrOfPinnedObject();
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;

        if (_handle.IsAllocated)
            _handle.Free();

        _ptr = IntPtr.Zero;
    }
}
