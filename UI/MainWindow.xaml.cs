using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using System;
using System.Collections.Generic;
using System.Numerics;
using System.Threading.Tasks;
using UI.Input;
using UI.ViewModels;

namespace UI;

/// <summary>
/// MainWindow：純 UI 層，只負責事件轉發與 ViewModel 綁定。
/// </summary>
public sealed partial class MainWindow : Window
{
    private readonly MainViewModel _vm;
    private CameraInputHandler?    _cameraInput;

    // TreeViewNode → NodeItem 映射表
    private readonly Dictionary<TreeViewNode, NodeItem> _nodeMap = new();

    // 每個模型的根 TreeViewNode，用於倇除模型時清除
    private readonly Dictionary<int, List<TreeViewNode>> _meshRootNodes = new();

    public MainWindow()
    {
        InitializeComponent();
        _vm = new MainViewModel();

        _vm.Hierarchy.OnNodeSelected += OnNodeSelected;

        _vm.IsLoadingChanged += isLoading =>
            LoadingOverlay.Visibility = isLoading ? Visibility.Visible : Visibility.Collapsed;

        CompositionTarget.Rendering += OnGameLoopTick;
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
        double scale = RenderPanel.XamlRoot.RasterizationScale;
        int w = (int)(RenderPanel.ActualWidth  * scale);
        int h = (int)(RenderPanel.ActualHeight * scale);
        if (w == 0 || h == 0) return;

        bool ok = _vm.Renderer.Init(RenderPanel, w, h);
        StatusText.Text = ok ? "DX12 Ready ✓" : "DX12 Init Failed ✗";

        if (ok) {
            _vm.Renderer.Resize(RenderPanel.ActualWidth, RenderPanel.ActualHeight, scale);
            _cameraInput = new CameraInputHandler(RenderPanel, _vm.Camera, GetSelectedNodeWorldPosition);
        }
    }

    private Vector3? GetSelectedNodeWorldPosition()
    {
        var node = _vm.Hierarchy.SelectedNode;
        if (node == null) return null;
        var (t, _, _) = _vm.Renderer.GetNodeTransform(node.GlobalIndex);
        return new Vector3(t[0], t[1], t[2]);
    }

    private void RenderPanel_SizeChanged(object sender, SizeChangedEventArgs e)
    {
        double scale = RenderPanel.XamlRoot.RasterizationScale;
        _vm.Renderer.Resize(e.NewSize.Width, e.NewSize.Height, scale);
    }

    private void RenderPanel_Unloaded(object sender, RoutedEventArgs e)
        => _vm.Renderer.Shutdown();

    // ── 模型載入 ───────────────────────────────────────

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

        var file = await picker.PickSingleFileAsync().AsTask();
        if (file == null) return;

        _vm.IsLoading = true;
        // 追加模型到場景（取代原本的取代語意）
        int meshId = await _vm.Renderer.AddModelAsync(file.Path);
        _vm.IsLoading = false;

        // 取得這個 mesh 的 node 數量
        // 注： C++ 節點數量可由 GetTotalNodeCount 前後差分析，
        //      或者暴挙地由 meshId * STRIDE 開始這時試到第一個無效 index。
        //      简化作法：利用 GetNodeInfo 回傳的失效來尌界。
        int localCount = CountNodesForMesh(meshId);
        AppendMeshToHierarchy(meshId, localCount);
    }

    /// <summary>
    /// 將指定 meshId 的所有節點追加到 TreeView。
    /// </summary>
    private void AppendMeshToHierarchy(int meshId, int localNodeCount)
    {
        _vm.Hierarchy.AddMeshNodes(_vm.Renderer, meshId, localNodeCount);

        // 記錄此 mesh 對應的所有 root TreeViewNode（用於稍後即時尋找删除）
        var meshRoots = new List<TreeViewNode>();
        _meshRootNodes[meshId] = meshRoots;

        AppendTreeNodes(
            _vm.Hierarchy.RootNodes
                .Where(n => n.MeshId == meshId)
                .ToList(),
            HierarchyTree.RootNodes,
            meshRoots);
    }

    /// <summary>加載後計算 mesh 的屬地 node 數量。</summary>
    private int CountNodesForMesh(int meshId)
    {
        int count = 0;
        int stride = UI.Services.RendererService.MeshNodeStride;
        byte[] buf = new byte[256];
        for (int local = 0; local < stride; local++)
        {
            int gIdx = meshId * stride + local;
            RenderBridge.Renderer_GetNodeInfo(gIdx, buf, buf.Length, out int _);
            if (buf[0] == 0) break; // 名稱為空字串代表超出範圍
            count++;
            buf[0] = 0; // 清除以便下次建檔
        }
        return count;
    }

    private void AppendTreeNodes(
        IList<NodeItem> source,
        IList<TreeViewNode> target,
        List<TreeViewNode>? rootTracker = null)
    {
        foreach (var item in source)
        {
            var node = new TreeViewNode { Content = item.Name, IsExpanded = true };
            _nodeMap[node] = item;

            // 綁定右鍵選單
            node.SetValue(FrameworkElement.TagProperty, item);

            target.Add(node);
            rootTracker?.Add(node);

            if (item.Children.Count > 0)
                AppendTreeNodes(item.Children, node.Children);
        }
    }

    // ── Hierarchy 事件 ─────────────────────────────────────

    private void HierarchyTree_ItemInvoked(TreeView sender, TreeViewItemInvokedEventArgs args)
    {
        if (args.InvokedItem is TreeViewNode treeNode &&
            _nodeMap.TryGetValue(treeNode, out var nodeItem))
        {
            _vm.Hierarchy.SelectedNode = nodeItem;
        }
    }

    private void OnNodeSelected(NodeItem? node)
    {
        if (node == null) return;
        NodeNameText.Text   = _vm.Transform.NodeName;
        PosXText.Text       = _vm.Transform.PX.ToString("F3");
        PosYText.Text       = _vm.Transform.PY.ToString("F3");
        PosZText.Text       = _vm.Transform.PZ.ToString("F3");
        RotXText.Text       = _vm.Transform.RX.ToString("F3");
        RotYText.Text       = _vm.Transform.RY.ToString("F3");
        RotZText.Text       = _vm.Transform.RZ.ToString("F3");
        ScaleXText.Text     = _vm.Transform.SX.ToString("F3");
        ScaleYText.Text     = _vm.Transform.SY.ToString("F3");
        ScaleZText.Text     = _vm.Transform.SZ.ToString("F3");
    }

    // ── 右鍵倇除模型 ───────────────────────────────────

    /// <summary>
    /// 在 TreeView 項目上右鍵，顯示包含「删除模型」的 ContextFlyout。
    /// 如果所選節點是根節點（parentIndex == -1）則允許删除；
    /// 子節點不允許単獨删除（必須删除整個模型）。
    /// </summary>
    private void HierarchyTreeItem_RightTapped(object sender, RightTappedRoutedEventArgs e)
    {
        if (sender is not FrameworkElement fe) return;
        var treeNode = fe.DataContext as TreeViewNode;
        if (treeNode == null || !_nodeMap.TryGetValue(treeNode, out var nodeItem)) return;

        // 只對根節點（模型頂層）提供删除選項
        if (nodeItem.ParentIndex != -1) return;

        var flyout = new MenuFlyout();
        var deleteItem = new MenuFlyoutItem { Text = "删除模型" };
        deleteItem.Click += (_, _) => DeleteModel(nodeItem.MeshId);
        flyout.Items.Add(deleteItem);
        flyout.ShowAt(fe, e.GetPosition(fe));

        e.Handled = true;
    }

    private void DeleteModel(int meshId)
    {
        // 1. 移除 C++ 端渲染資料
        _vm.Renderer.RemoveModel(meshId);

        // 2. 清除 ViewModel 樹狀
        _vm.Hierarchy.RemoveMeshNodes(meshId);

        // 3. 清除 TreeView UI
        if (_meshRootNodes.TryGetValue(meshId, out var roots)) {
            foreach (var r in roots) {
                HierarchyTree.RootNodes.Remove(r);
                _nodeMap.Remove(r);
            }
            _meshRootNodes.Remove(meshId);
        }

        // 4. 如果選取的節點屬於此 mesh，清除選取
        if (_vm.Hierarchy.SelectedNode?.MeshId == meshId)
            _vm.Hierarchy.SelectedNode = null;
    }

    // ── Transform Inspector ─────────────────────────────────

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

    // ── 工具 ─────────────────────────────────────────────

    private IEnumerable<NodeItem> GetNodesForMesh(int meshId)
        => _nodeMap.Values.Where(n => n.MeshId == meshId);
}
