using System;
using System.Collections.Generic;
using UI.Services;

namespace UI.ViewModels;

/// <summary>
/// 聚合所有 ViewModel，是 MainWindow 唯一持有的頂層物件。
/// </summary>
internal sealed class MainViewModel : IDisposable
{
    public RendererService    Renderer  { get; } = new();
    public CameraViewModel    Camera    { get; } = new();
    public StatsViewModel     Stats     { get; } = new();
    public HierarchyViewModel Hierarchy { get; } = new();
    public TransformViewModel Transform { get; }

    // 批次刷入用的 batcher 與暫存記憶體
    private readonly NodeTransformBatcher _batcher = new();
    private readonly List<NodeEntry>      _dirtyEntries = new();

    private bool _isLoading;
    public bool IsLoading
    {
        get => _isLoading;
        set { _isLoading = value; IsLoadingChanged?.Invoke(_isLoading); }
    }

    public event Action<bool>? IsLoadingChanged;

    public MainViewModel()
    {
        Transform = new TransformViewModel(Renderer);
        Hierarchy.OnNodeSelected += node => Transform.LoadNode(node);
    }

    /// <summary>
    /// 每幀由 GameLoop 呼叫：
    /// 1. 推送相機狀態
    /// 2. 收集所有 dirty node 的 Transform，單次 P/Invoke 刷入 C++
    /// 3. 更新效能統計
    /// </summary>
    public void Tick()
    {
        // 1. 相機
        var c = Camera;
        Renderer.SetCamera(c.Position.X, c.Position.Y, c.Position.Z, c.Pitch, c.Yaw);

        // 2. 收集 dirty Transform 并批次刷入
        //    目前只有一個 TransformViewModel 可被編輯，
        //    未來支援多個同時編輯時可擴展為遍走所有 VM
        if (Transform.IsDirty)
        {
            _dirtyEntries.Clear();

            // 目前只有單點被修改：直接封裝為單元素清單
            // 如果未來需要批量點同時修改，可改為遍走 TransformViewModels
            int nodeCount = Renderer.GetNodeCount();
            for (int i = 0; i < nodeCount; i++)
            {
                // 只有目前選取點被標記為 dirty。
                // 若是選取點則取新 entry，其餘取 C++ 現有資料
                if (i == Transform.NodeIndex && Transform.IsDirty)
                {
                    _dirtyEntries.Add(Transform.BuildEntry());
                }
                else
                {
                    var (t, r, s) = Renderer.GetNodeTransform(i);
                    _dirtyEntries.Add(NodeEntry.FromArrays(t, r, s));
                }
            }

            Renderer.FlushNodeTransforms(_batcher, _dirtyEntries);

            // 清除 dirty flag （本幀已刷入）
            Transform.ClearDirty();
        }

        // 3. 效能統計
        var (v, p, dc, ft) = Renderer.GetStats();
        Stats.Update(v, p, dc, ft);
    }

    public void Dispose() => _batcher.Dispose();
}
