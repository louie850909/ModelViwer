namespace UI.ViewModels;

/// <summary>
/// 每幀效能數據。由 MainViewModel.Tick() 更新。
/// </summary>
internal sealed class StatsViewModel : ObservableObject
{
    private float _frameTimeMs;
    private int   _vertices;
    private int   _polygons;
    private int   _drawCalls;

    public float FrameTimeMs { get => _frameTimeMs; private set => SetProperty(ref _frameTimeMs, value); }
    public int   Vertices    { get => _vertices;    private set => SetProperty(ref _vertices,    value); }
    public int   Polygons    { get => _polygons;    private set => SetProperty(ref _polygons,    value); }
    public int   DrawCalls   { get => _drawCalls;   private set => SetProperty(ref _drawCalls,   value); }

    /// <summary>格式化後的多行顯示字串，供 TextBlock 直接綁定。</summary>
    public string DisplayText =>
        $"Frame Time : {FrameTimeMs:F2} ms\n" +
        $"Vertices   : {Vertices:N0}\n" +
        $"Polygons   : {Polygons:N0}\n" +
        $"Draw Calls : {DrawCalls}";

    public void Update(int vertices, int polygons, int drawCalls, float frameTimeMs)
    {
        FrameTimeMs = frameTimeMs;
        Vertices    = vertices;
        Polygons    = polygons;
        DrawCalls   = drawCalls;
        OnPropertyChanged(nameof(DisplayText));
    }
}
