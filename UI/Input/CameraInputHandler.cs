using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Controls;
using System;
using System.Numerics;
using Windows.System;
using UI.ViewModels;

namespace UI.Input;

/// <summary>
/// 將 SwapChainPanel 的滑鼠/鍵盤事件轉換成 CameraViewModel 的操作指令。
/// MainWindow 只需建立此 Handler 並傳入 Panel 即可，不含任何相機數學。
/// </summary>
internal sealed class CameraInputHandler
{
    private readonly CameraViewModel    _camera;
    private readonly SwapChainPanel     _panel;

    /// <summary>
    /// 取得目前選取節點的世界座標。
    /// 由外部提供 (MainViewModel 注入)，讓 Handler 不需要知道 ViewModel 內部細節。
    /// 回傳 null 代表目前沒有選取節點。
    /// </summary>
    private readonly Func<Vector3?> _getSelectedNodePosition;

    private bool _isOrbiting   = false;
    private bool _isFPSLooking = false;
    private bool _wasFKeyDown  = false; // 違緣偵測，避免每幀重複觸發
    private Windows.Foundation.Point _lastPos;

    public CameraInputHandler(
        SwapChainPanel panel,
        CameraViewModel camera,
        Func<Vector3?> getSelectedNodePosition)
    {
        _panel                   = panel;
        _camera                  = camera;
        _getSelectedNodePosition = getSelectedNodePosition;

        _panel.PointerPressed      += OnPointerPressed;
        _panel.PointerMoved        += OnPointerMoved;
        _panel.PointerReleased     += OnPointerReleased;
        _panel.PointerWheelChanged += OnPointerWheelChanged;
    }

    /// <summary>
    /// 每幀由 GameLoop 呼叫，處理 WASD 持續移動與 F 鍵對焦。
    /// </summary>
    public void TickMovement()
    {
        // ─ WASD 移動 ─────────────────────────────────
        if (_isFPSLooking)
        {
            float speed = _camera.MoveSpeed;
            float dR = 0f, dU = 0f, dF = 0f;

            if (IsKeyDown(VirtualKey.W)) dF += speed;
            if (IsKeyDown(VirtualKey.S)) dF -= speed;
            if (IsKeyDown(VirtualKey.A)) dR -= speed;
            if (IsKeyDown(VirtualKey.D)) dR += speed;
            if (IsKeyDown(VirtualKey.E)) dU += speed;
            if (IsKeyDown(VirtualKey.Q)) dU -= speed;

            _camera.ApplyMove(dR, dU, dF);
        }

        // ─ F 鍵對焦 (遷跃偵測，按一次只觸發一次) ───────────
        bool isFKeyDown = IsKeyDown(VirtualKey.F);
        if (isFKeyDown && !_wasFKeyDown)
        {
            var pos = _getSelectedNodePosition();
            if (pos.HasValue)
                _camera.FocusOn(pos.Value);
        }
        _wasFKeyDown = isFKeyDown;
    }

    // ── 事件處理 ─────────────────────────────────────────

    private void OnPointerPressed(object sender, PointerRoutedEventArgs e)
    {
        var pt = e.GetCurrentPoint(_panel);
        bool isAlt = IsKeyDown(VirtualKey.Menu);

        if (pt.Properties.IsLeftButtonPressed && isAlt)
        {
            _isOrbiting = true;
            _lastPos    = pt.Position;
            _panel.CapturePointer(e.Pointer);
        }
        else if (pt.Properties.IsRightButtonPressed)
        {
            _isFPSLooking = true;
            _lastPos      = pt.Position;
            _panel.CapturePointer(e.Pointer);
        }
    }

    private void OnPointerMoved(object sender, PointerRoutedEventArgs e)
    {
        if (!_isOrbiting && !_isFPSLooking) return;

        var pt = e.GetCurrentPoint(_panel);
        float dx = (float)(pt.Position.X - _lastPos.X);
        float dy = (float)(pt.Position.Y - _lastPos.Y);
        _lastPos = pt.Position;

        if (_isOrbiting)  _camera.ApplyOrbit(dx, dy);
        else              _camera.ApplyFPSLook(dx, dy);
    }

    private void OnPointerReleased(object sender, PointerRoutedEventArgs e)
    {
        var pt = e.GetCurrentPoint(_panel);
        if (!pt.Properties.IsLeftButtonPressed && _isOrbiting)
        {
            _isOrbiting = false;
            _panel.ReleasePointerCapture(e.Pointer);
        }
        if (!pt.Properties.IsRightButtonPressed && _isFPSLooking)
        {
            _isFPSLooking = false;
            _panel.ReleasePointerCapture(e.Pointer);
        }
    }

    private void OnPointerWheelChanged(object sender, PointerRoutedEventArgs e)
    {
        float delta = e.GetCurrentPoint(_panel).Properties.MouseWheelDelta;
        if (_isFPSLooking) _camera.AdjustMoveSpeed(delta);
        else               _camera.ApplyZoom(delta);
    }

    private static bool IsKeyDown(VirtualKey key) =>
        Microsoft.UI.Input.InputKeyboardSource
            .GetKeyStateForCurrentThread(key)
            .HasFlag(Windows.UI.Core.CoreVirtualKeyStates.Down);
}
