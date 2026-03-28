using System;
using System.Numerics;

namespace UI.ViewModels;

/// <summary>
/// 相機狀態 (Source of Truth)。
/// 持有 Yaw/Pitch/Position/OrbitRadius，並提供 Orbit、FPS Look、Zoom、WASD 方法。
/// 不直接接觸任何 WinUI / P/Invoke API。
/// </summary>
internal sealed class CameraViewModel : ObservableObject
{
    // ── State ────────────────────────────────────────────
    public Vector3 Position    { get; private set; } = new(0f, 0f, -3f);
    public float   Yaw         { get; private set; } = 0f;
    public float   Pitch       { get; private set; } = 0f;
    public float   OrbitRadius { get; private set; } = 3f;
    public float   MoveSpeed   { get; private set; } = 0.05f;

    private const float PitchLimit = 1.56f;
    private const float MinSpeed   = 0.001f;
    private const float MaxSpeed   = 5.0f;

    // ── 公開操作方法 ─────────────────────────────────────

    /// <summary>Orbit 旋轉 (Alt + 左鍵拖曳)。</summary>
    public void ApplyOrbit(float dx, float dy, float sensitivity = 0.005f)
    {
        // 1. 計算 Pivot
        Matrix4x4 oldRot = Matrix4x4.CreateFromYawPitchRoll(Yaw, Pitch, 0f);
        Vector3 oldForward = Vector3.Transform(Vector3.UnitZ, oldRot);
        Vector3 pivot = Position + oldForward * OrbitRadius;

        // 2. 更新角度
        float newYaw   = Yaw   + dx * sensitivity;
        float newPitch = Math.Clamp(Pitch + dy * sensitivity, -PitchLimit, PitchLimit);

        // 3. 重新計算位置
        Matrix4x4 newRot = Matrix4x4.CreateFromYawPitchRoll(newYaw, newPitch, 0f);
        Vector3 newForward = Vector3.Transform(Vector3.UnitZ, newRot);

        Yaw      = newYaw;
        Pitch    = newPitch;
        Position = pivot - newForward * OrbitRadius;
    }

    /// <summary>第一人稱視角旋轉 (右鍵拖曳)。</summary>
    public void ApplyFPSLook(float dx, float dy, float sensitivity = 0.005f)
    {
        Yaw   += dx * sensitivity;
        Pitch  = Math.Clamp(Pitch + dy * sensitivity, -PitchLimit, PitchLimit);
    }

    /// <summary>WASD 移動，每幀呼叫一次。</summary>
    public void ApplyMove(float right, float up, float forward)
    {
        if (right == 0f && up == 0f && forward == 0f) return;
        Matrix4x4 rot = Matrix4x4.CreateFromYawPitchRoll(Yaw, Pitch, 0f);
        Vector3 fwd = Vector3.Transform(Vector3.UnitZ, rot);
        Vector3 rgt = Vector3.Transform(Vector3.UnitX, rot);
        Position += rgt * right + Vector3.UnitY * up + fwd * forward;
    }

    /// <summary>滾輪縮放 (無右鍵模式)。</summary>
    public void ApplyZoom(float delta, float sensitivity = 0.005f)
    {
        float dF = delta * sensitivity;
        Matrix4x4 rot = Matrix4x4.CreateFromYawPitchRoll(Yaw, Pitch, 0f);
        Vector3 forward = Vector3.Transform(Vector3.UnitZ, rot);
        Position     += forward * dF;
        OrbitRadius   = Math.Max(0.1f, OrbitRadius - dF);
    }

    /// <summary>按住右鍵時滾輪調整飛行速度。</summary>
    public void AdjustMoveSpeed(float delta)
    {
        float mult = delta > 0 ? 1.2f : 0.8f;
        MoveSpeed = Math.Clamp(MoveSpeed * mult, MinSpeed, MaxSpeed);
    }
}
