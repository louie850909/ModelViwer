using System;
using System.Numerics;
using UI.Services;

namespace UI.ViewModels;

/// <summary>
/// 負責選取節點的 TRS 資料顯示與回寫，以及 Euler ↔ Quaternion 轉換。
/// </summary>
internal sealed class TransformViewModel : ObservableObject
{
    private readonly RendererService _renderer;
    private int _nodeIndex = -1;

    public TransformViewModel(RendererService renderer)
        => _renderer = renderer;

    // ── Position ─────────────────────────────────────
    private float _px, _py, _pz;
    public float PX { get => _px; set => SetProperty(ref _px, value); }
    public float PY { get => _py; set => SetProperty(ref _py, value); }
    public float PZ { get => _pz; set => SetProperty(ref _pz, value); }

    // ── Rotation (Euler 度數) ─────────────────────────
    private float _rx, _ry, _rz;
    public float RX { get => _rx; set => SetProperty(ref _rx, value); }
    public float RY { get => _ry; set => SetProperty(ref _ry, value); }
    public float RZ { get => _rz; set => SetProperty(ref _rz, value); }

    // ── Scale ────────────────────────────────────────
    private float _sx = 1f, _sy = 1f, _sz = 1f;
    public float SX { get => _sx; set => SetProperty(ref _sx, value); }
    public float SY { get => _sy; set => SetProperty(ref _sy, value); }
    public float SZ { get => _sz; set => SetProperty(ref _sz, value); }

    private string _nodeName = string.Empty;
    public string NodeName { get => _nodeName; private set => SetProperty(ref _nodeName, value); }

    /// <summary>
    /// 目前選取節點的 CppIndex。
    /// MainViewModel 用於建立批次資料時定位要寫入的 index。
    /// </summary>
    public int NodeIndex => _nodeIndex;

    /// <summary>
    /// 標記此幀是否有未同步的變更，等候 Tick() 批次刷入 C++。
    /// Apply() 與 TryApplyFromStrings() 被呼叫時自動設為 true。
    /// </summary>
    public bool IsDirty { get; private set; } = false;

    // ── 公開操作 ─────────────────────────────────────

    /// <summary>載入指定節點的 Transform 到 VM 屬性。</summary>
    public void LoadNode(NodeItem? node)
    {
        if (node == null) { _nodeIndex = -1; NodeName = string.Empty; return; }

        _nodeIndex = node.CppIndex;
        NodeName   = node.Name;

        var (t, r, s) = _renderer.GetNodeTransform(_nodeIndex);
        PX = t[0]; PY = t[1]; PZ = t[2];
        SX = s[0]; SY = s[1]; SZ = s[2];

        var euler = QuatToEulerDeg(new Quaternion(r[0], r[1], r[2], r[3]));
        RX = euler.X; RY = euler.Y; RZ = euler.Z;

        IsDirty = false; // 剛載入不算 dirty
    }

    /// <summary>
    /// 將目前 VM 的屬性轉成 NodeEntry，由 MainViewModel.Tick() 收集使用。
    /// 不直接發起 P/Invoke，诈打包放到批次作業。
    /// </summary>
    public NodeEntry BuildEntry()
    {
        float pitch = RX * (float)(Math.PI / 180.0);
        float yaw   = RY * (float)(Math.PI / 180.0);
        float roll  = RZ * (float)(Math.PI / 180.0);
        Quaternion q = Quaternion.CreateFromYawPitchRoll(yaw, pitch, roll);
        return new NodeEntry(
            PX, PY, PZ,
            q.X, q.Y, q.Z, q.W,
            SX, SY, SZ);
    }

    /// <summary>
    /// 將目前 VM 的傀寫回 C++（單點立即生效，保留相容）。
    /// 同時設置 IsDirty = true，讓 Tick() 得知需要批次刷新。
    /// </summary>
    public void Apply()
    {
        if (_nodeIndex < 0) return;
        IsDirty = true;

        float[] t = { PX, PY, PZ };
        float[] s = { SX, SY, SZ };

        float pitch = RX * (float)(Math.PI / 180.0);
        float yaw   = RY * (float)(Math.PI / 180.0);
        float roll  = RZ * (float)(Math.PI / 180.0);
        Quaternion q = Quaternion.CreateFromYawPitchRoll(yaw, pitch, roll);
        float[] r = { q.X, q.Y, q.Z, q.W };

        _renderer.SetNodeTransform(_nodeIndex, t, r, s);
    }

    /// <summary>嘗試從字串套用，輸入非數字時靜默略過。</summary>
    public bool TryApplyFromStrings(
        string px, string py, string pz,
        string rx, string ry, string rz,
        string sx, string sy, string sz)
    {
        if (!float.TryParse(px, out float fPX) || !float.TryParse(py, out float fPY) || !float.TryParse(pz, out float fPZ)) return false;
        if (!float.TryParse(rx, out float fRX) || !float.TryParse(ry, out float fRY) || !float.TryParse(rz, out float fRZ)) return false;
        if (!float.TryParse(sx, out float fSX) || !float.TryParse(sy, out float fSY) || !float.TryParse(sz, out float fSZ)) return false;

        PX = fPX; PY = fPY; PZ = fPZ;
        RX = fRX; RY = fRY; RZ = fRZ;
        SX = fSX; SY = fSY; SZ = fSZ;
        Apply();
        return true;
    }

    // ── 轉換工具 ─────────────────────────────────────

    private static Vector3 QuatToEulerDeg(Quaternion q)
    {
        double sinr_cosp = 2 * (q.W * q.X + q.Y * q.Z);
        double cosr_cosp = 1 - 2 * (q.X * q.X + q.Y * q.Y);
        float rx = (float)Math.Atan2(sinr_cosp, cosr_cosp);

        double sinp = 2 * (q.W * q.Y - q.Z * q.X);
        float ry = Math.Abs(sinp) >= 1
            ? (float)Math.CopySign(Math.PI / 2, sinp)
            : (float)Math.Asin(sinp);

        double siny_cosp = 2 * (q.W * q.Z + q.X * q.Y);
        double cosy_cosp = 1 - 2 * (q.Y * q.Y + q.Z * q.Z);
        float rz = (float)Math.Atan2(siny_cosp, cosy_cosp);

        const float toDeg = (float)(180.0 / Math.PI);
        return new Vector3(rx * toDeg, ry * toDeg, rz * toDeg);
    }
}
