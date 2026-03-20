using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using System;
using System.Runtime.InteropServices;
using WinRT;                          // ABI interop (NuGet: Microsoft.Windows.CsWinRT)

namespace UI;

public sealed partial class MainWindow : Window
{
    private bool _rendererInitialized = false;

    public MainWindow() => InitializeComponent();

    private void RenderPanel_Loaded(object sender, RoutedEventArgs e)
    {
        int w = (int)RenderPanel.ActualWidth;
        int h = (int)RenderPanel.ActualHeight;
        if (w == 0 || h == 0) return;

        // 取得 SwapChainPanel 的 IUnknown* 指標傳給 C++ DLL
        // WinUI 3 中透過 IWinRTObject 取得原生 COM 指標
        var panelObj = RenderPanel.As<IWinRTObject>();
        IntPtr unknownPtr = MarshalInspectable<SwapChainPanel>.FromManaged(RenderPanel);

        bool ok = RenderBridge.Renderer_Init(unknownPtr, w, h);
        StatusText.Text = ok ? "DX12 Ready ✓" : "DX12 Init Failed ✗";
        _rendererInitialized = ok;
    }

    private void RenderPanel_SizeChanged(object sender, SizeChangedEventArgs e)
    {
        if (!_rendererInitialized) return;
        int w = (int)e.NewSize.Width;
        int h = (int)e.NewSize.Height;
        if (w > 0 && h > 0) RenderBridge.Renderer_Resize(w, h);
    }

    private void RenderPanel_Unloaded(object sender, RoutedEventArgs e)
    {
        if (_rendererInitialized) RenderBridge.Renderer_Shutdown();
    }
}