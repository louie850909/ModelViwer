using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using System;
using UI.Input;
using UI.ViewModels;

namespace UI;

/// <summary>
/// MainWindow：純 UI 層，只負責事件轉發與 ViewModel 綁定。
/// 不包含任何業務邏輯、相機數學或 P/Invoke 呼叫。
/// </summary>
public sealed partial class MainWindow : Window
{
    private readonly MainViewModel      _vm;
    private readonly CameraInputHandler _cameraInput;

    // 用來記錄 TreeViewNode → NodeItem 的映射
    private System.Collections.Generic.Dictionary<TreeViewNode, ViewModels.NodeItem> _nodeMap = new();

    public MainWindow()
    {
        InitializeComponent();
        _vm = new MainViewModel();

        // Hierarchy 選取變更時同步 Inspector 面板
        _vm.Hierarchy.OnNodeSelected += OnNodeSelected;

        // 載入中狀態變更時切換 LoadingOverlay
        _vm.IsLoadingChanged += isLoading =>
            LoadingOverlay.Visibility = isLoading ? Visibility.Visible : Visibility.Collapsed;

        // 每幀更新 (GameLoop)
        CompositionTarget.Rendering += OnGameLoopTick;

        // 輸入處理器延遲到 Panel 載入後再建立
        _cameraInput = null!; // 先佔位，RenderPanel_Loaded 中初始化
    }

    // ── GameLoop ─────────────────────────────────────────

    private void OnGameLoopTick(object? sender, object e)
    {
        if (!_vm.Renderer.IsInitialized) return;
        _cameraInput?.TickMovement();
        _vm.Tick();
        StatsText.Text = _vm.Stats.DisplayText;
    }

    // ── Renderer 生命週期 ─────────────────────────────────

    private void RenderPanel_Loaded(object sender, RoutedEventArgs e)
    {
        int w = (int)RenderPanel.ActualWidth;
        int h = (int)RenderPanel.ActualHeight;
        if (w == 0 || h == 0) return;

        bool ok = _vm.Renderer.Init(RenderPanel, w, h);
        StatusText.Text = ok ? "DX12 Ready ✓" : "DX12 Init Failed ✗";

        if (ok)
        {
            // 輸入處理器在 Renderer 初始化後才綁定
            var inputField = typeof(MainWindow).GetField("_cameraInput",
                System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
            inputField?.SetValue(this, new CameraInputHandler(RenderPanel, _vm.Camera));
        }
    }

    private void RenderPanel_SizeChanged(object sender, SizeChangedEventArgs e)
    {
        double scale = RenderPanel.XamlRoot.RasterizationScale;
        _vm.Renderer.Resize(e.NewSize.Width, e.NewSize.Height, scale);
    }

    private void RenderPanel_Unloaded(object sender, RoutedEventArgs e)
        => _vm.Renderer.Shutdown();

    // ── 模型載入 ─────────────────────────────────────────

    private async void OpenModel_Click(object sender, RoutedEventArgs e)
    {
        if (!_vm.Renderer.IsInitialized) return;

        var picker = new Windows.Storage.Pickers.FileOpenPicker();
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        WinRT.Interop.InitializeWithWindow.Initialize(picker, hwnd);
        picker.FileTypeFilter.Add(".fbx");
        picker.FileTypeFilter.Add(".gltf");
        picker.FileTypeFilter.Add(".glb");
        picker.FileTypeFilter.Add(".obj");

        var file = await picker.PickSingleFileAsync();
        if (file == null) return;

        _vm.IsLoading = true;

        // async/await 取代 callback 回呼，更直觀
        await _vm.Renderer.LoadModelAsync(file.Path);

        // 回到 UI 執行緒後直接更新
        _vm.IsLoading = false;
        RebuildHierarchyTree();
    }

    // ── Hierarchy ────────────────────────────────────────

    private void RebuildHierarchyTree()
    {
        _vm.Hierarchy.Rebuild(_vm.Renderer);
        _nodeMap.Clear();
        HierarchyTree.RootNodes.Clear();
        BuildTreeNodes(_vm.Hierarchy.RootNodes, HierarchyTree.RootNodes);
    }

    private void BuildTreeNodes(
        System.Collections.ObjectModel.ObservableCollection<ViewModels.NodeItem> source,
        Microsoft.UI.Xaml.Controls.TreeViewNodeCollection target)
    {
        foreach (var item in source)
        {
            var node = new TreeViewNode { Content = item.Name, IsExpanded = true };
            _nodeMap[node] = item;
            target.Add(node);
            if (item.Children.Count > 0)
                BuildTreeNodes(item.Children, node.Children);
        }
    }

    private void HierarchyTree_ItemInvoked(TreeView sender, TreeViewItemInvokedEventArgs args)
    {
        if (args.InvokedItem is TreeViewNode treeNode &&
            _nodeMap.TryGetValue(treeNode, out var nodeItem))
        {
            _vm.Hierarchy.SelectedNode = nodeItem;
        }
    }

    private void OnNodeSelected(ViewModels.NodeItem? node)
    {
        if (node == null) return;
        NodeNameText.Text  = _vm.Transform.NodeName;
        PosXText.Text = _vm.Transform.PX.ToString("F3");
        PosYText.Text = _vm.Transform.PY.ToString("F3");
        PosZText.Text = _vm.Transform.PZ.ToString("F3");
        RotXText.Text = _vm.Transform.RX.ToString("F3");
        RotYText.Text = _vm.Transform.RY.ToString("F3");
        RotZText.Text = _vm.Transform.RZ.ToString("F3");
        ScaleXText.Text = _vm.Transform.SX.ToString("F3");
        ScaleYText.Text = _vm.Transform.SY.ToString("F3");
        ScaleZText.Text = _vm.Transform.SZ.ToString("F3");
    }

    // ── Transform Inspector ───────────────────────────────

    private void TransformInput_KeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key == Windows.System.VirtualKey.Enter) ApplyTransform();
    }

    private void TransformInput_LostFocus(object sender, RoutedEventArgs e)
        => ApplyTransform();

    private void ApplyTransform()
    {
        _vm.Transform.TryApplyFromStrings(
            PosXText.Text,   PosYText.Text,   PosZText.Text,
            RotXText.Text,   RotYText.Text,   RotZText.Text,
            ScaleXText.Text, ScaleYText.Text, ScaleZText.Text);
    }
}
