using DesktopSplitter.Models;

namespace DesktopSplitter.ViewModels;

/// <summary>Editable resolution for a single layout segment.</summary>
public sealed class SegmentViewModel : ObservableObject
{
    private string _widthText = string.Empty;
    private string _heightText = string.Empty;
    private string _error = string.Empty;
    private bool _suspendChanged;

    public SegmentViewModel(int index, int width, int height)
    {
        Index = index;
        _widthText = width.ToString();
        _heightText = height.ToString();
        Validate();
    }

    public int Index { get; }

    public string Title => $"Zone {Index + 1}";

    public string WidthText
    {
        get => _widthText;
        set
        {
            if (!SetProperty(ref _widthText, value)) return;
            Validate();
            if (!_suspendChanged) RaiseChanged();
        }
    }

    public string HeightText
    {
        get => _heightText;
        set
        {
            if (!SetProperty(ref _heightText, value)) return;
            Validate();
            if (!_suspendChanged) RaiseChanged();
        }
    }

    public string Error
    {
        get => _error;
        private set { if (SetProperty(ref _error, value)) OnPropertyChanged(nameof(HasError)); }
    }

    public bool HasError => !string.IsNullOrEmpty(Error);

    public bool IsValid => !HasError;

    public int Width => int.TryParse(_widthText, out int w) ? w : 0;
    public int Height => int.TryParse(_heightText, out int h) ? h : 0;

    /// <summary>Raised whenever the edited value changes, so the parent can re-evaluate Apply.</summary>
    public event EventHandler? Changed;

    private void RaiseChanged() => Changed?.Invoke(this, EventArgs.Empty);

    /// <summary>
    /// Pushes a new size from the splitter / layout maths. Always raises PropertyChanged so
    /// bound TextBoxes refresh even when WPF TwoWay bindings would otherwise keep a stale
    /// target value during/after a drag.
    /// </summary>
    public void Set(int width, int height)
    {
        string wText = width.ToString();
        string hText = height.ToString();
        bool widthChanged = !string.Equals(_widthText, wText, StringComparison.Ordinal);
        bool heightChanged = !string.Equals(_heightText, hText, StringComparison.Ordinal);

        _suspendChanged = true;
        try
        {
            _widthText = wText;
            _heightText = hText;
            // Always notify: SetProperty would skip equal values, and some TwoWay TextBox
            // bindings also ignore a no-op; drag UI must refresh every move.
            OnPropertyChanged(nameof(WidthText));
            OnPropertyChanged(nameof(HeightText));
            OnPropertyChanged(nameof(Width));
            OnPropertyChanged(nameof(Height));
            Validate();
        }
        finally { _suspendChanged = false; }

        if (widthChanged || heightChanged) RaiseChanged();
    }

    private void Validate()
    {
        if (!int.TryParse(_widthText, out int w) || !int.TryParse(_heightText, out int h))
        {
            Error = "Enter whole numbers.";
            return;
        }
        int lo = LayoutGeometry.MinSegmentDimension;
        int hi = LayoutGeometry.MaxSegmentDimension;
        if (w < lo || w > hi || h < lo || h > hi)
        {
            Error = $"Width and height must be {lo}..{hi}.";
            return;
        }
        Error = string.Empty;
    }
}
