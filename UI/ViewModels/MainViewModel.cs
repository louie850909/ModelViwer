using UI.Services;

namespace UI.ViewModels;

/// <summary>
/// 聚合所有 ViewModel，是 MainWindow 唯一持有的頂層物件。
/// </summary>
internal sealed class MainViewModel
{
    public RendererService   Renderer  { get; } = new();
    public CameraViewModel   Camera    { get; } = new();
    public StatsViewModel    Stats     { get; } = new();
    public HierarchyViewModel Hierarchy { get; } = new();
    public TransformViewModel Transform { get; }

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

        // 節點選取時，自動通知 TransformViewModel 更新
        Hierarchy.OnNodeSelected += node => Transform.LoadNode(node);
    }

    /// <summary>
    /// 每幀由 GameLoop 呼叫：推送相機狀態、更新效能數據。
    /// </summary>
    public void Tick()
    {
        var c = Camera;
        Renderer.SetCamera(c.Position.X, c.Position.Y, c.Position.Z, c.Pitch, c.Yaw);

        var (v, p, dc, ft) = Renderer.GetStats();
        Stats.Update(v, p, dc, ft);
    }
}
