using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using UI.Services;

namespace UI.ViewModels;

/// <summary>
/// 一個 UI 無關的 Node 資料節點，供 TreeView 顯示。
/// </summary>
internal sealed class NodeItem
{
    public string Name        { get; init; } = string.Empty;
    public int    CppIndex    { get; init; }
    public int    ParentIndex { get; init; }
    public ObservableCollection<NodeItem> Children { get; } = new();
}

/// <summary>
/// 管理 Hierarchy 面板的節點樹狀結構與選取狀態。
/// </summary>
internal sealed class HierarchyViewModel : ObservableObject
{
    public ObservableCollection<NodeItem> RootNodes { get; } = new();

    private NodeItem? _selectedNode;
    public NodeItem? SelectedNode
    {
        get => _selectedNode;
        set
        {
            if (_selectedNode == value) return;
            _selectedNode = value;
            OnPropertyChanged();
            OnNodeSelected?.Invoke(_selectedNode);
        }
    }

    /// <summary>節點選取變更時觸發，通知 TransformViewModel 更新。</summary>
    public event Action<NodeItem?>? OnNodeSelected;

    public void Rebuild(RendererService renderer)
    {
        RootNodes.Clear();
        int count = renderer.GetNodeCount();
        if (count == 0) return;

        var indexMap = new Dictionary<int, NodeItem>();
        for (int i = 0; i < count; i++)
        {
            var (name, parentIndex) = renderer.GetNodeInfo(i);
            var item = new NodeItem { Name = name, CppIndex = i, ParentIndex = parentIndex };
            indexMap[i] = item;

            if (parentIndex == -1)
                RootNodes.Add(item);
            else if (indexMap.TryGetValue(parentIndex, out var parent))
                parent.Children.Add(item);
        }
    }
}
