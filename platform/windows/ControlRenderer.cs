using System.Drawing;
using System.Drawing.Drawing2D;
using System.Text.Json;
using System.Windows.Forms;

namespace Allwhither.Windows;

internal sealed class ControlRenderer
{
    private static readonly object ControlRootTag = new();
    private readonly JsonElement controls;
    private readonly ToolStripStatusLabel status;
    private readonly UiDom dom;
    private readonly JavaScriptActionRuntime actionRuntime;
    private int width;
    private int availableHeight;
    private readonly Dictionary<Control, int> widthOffsets = new();
    private readonly Dictionary<Control, JsonElement> groups = new();
    private readonly Dictionary<Control, ControlSizeSpec> sizes = new();
    private readonly HashSet<Control> wrappingLabels = new();
    private readonly Dictionary<Control, int> maxWidths = new();
    private readonly Dictionary<Control, JsonElement> responsiveVisibility = new();
    private readonly Color? themeBackground;
    private readonly Color? themeForeground;

    internal ControlRenderer(
        JsonElement controls, ToolStripStatusLabel status, UiDom dom,
        JavaScriptActionRuntime actionRuntime, int width, UiDefinition.ThemeColors theme)
    {
        this.controls = controls;
        this.status = status;
        this.dom = dom;
        this.actionRuntime = actionRuntime;
        this.width = width;
        themeBackground = theme.Background;
        themeForeground = theme.Foreground;
    }

    internal Control Render(string controlId)
    {
        JsonElement spec = controls.GetProperty(controlId);
        Control control = RenderControlContent(controlId);
        if (spec.TryGetProperty("margin", out JsonElement margin))
        {
            int pixels = UiDefinition.GetNonNegativeInt(spec, "margin");
            control.Margin = new Padding(pixels);
        }
        if (spec.TryGetProperty("padding", out JsonElement padding))
        {
            int pixels = UiDefinition.GetNonNegativeInt(spec, "padding");
            control.Padding = new Padding(pixels);
        }
        if (spec.TryGetProperty("maxWidth", out _))
        {
            int maxWidth = UiDefinition.GetPositiveInt(spec, "maxWidth");
            maxWidths[control] = maxWidth;
            control.MaximumSize = new Size(
                Math.Max(1, Math.Min(maxWidth, width - control.Margin.Horizontal)), 0);
        }
        UiDefinition.ControlDimension? controlWidth =
            spec.TryGetProperty("width", out _)
                ? UiDefinition.GetDimension(spec, "width") : null;
        UiDefinition.ControlDimension? controlHeight =
            spec.TryGetProperty("height", out _)
                ? UiDefinition.GetDimension(spec, "height") : null;
        if (controlWidth is UiDefinition.ControlDimension widthDimension)
        {
            control.AutoSize = false;
            control.Width = widthDimension.Resolve(width);
        }
        if (controlHeight is UiDefinition.ControlDimension heightDimension &&
            (UiDefinition.GetString(spec, "type") != "scrollPanel" ||
             heightDimension.Percent is not null))
        {
            control.AutoSize = false;
            if (heightDimension.Percent is null || availableHeight > 0)
            {
                control.Height = heightDimension.Resolve(availableHeight) +
                    (UiDefinition.GetString(spec, "type") == "scrollPanel" ? 28 : 0);
            }
        }
        bool center = spec.TryGetProperty("center", out JsonElement centerValue) &&
            centerValue.GetBoolean();
        if (controlWidth is not null || controlHeight is not null || center)
        {
            sizes[control] = new ControlSizeSpec(
                controlWidth,
                UiDefinition.GetString(spec, "type") == "scrollPanel" &&
                    controlHeight is { Percent: null } ? null : controlHeight,
                center,
                control.Margin,
                UiDefinition.GetString(spec, "type") == "scrollPanel");
            if (center)
            {
                    int extra = Math.Max(0, width - control.Width -
                        control.Margin.Left - control.Margin.Right) / 2;
                    control.Margin = new Padding(
                        control.Margin.Left + extra, control.Margin.Top,
                        control.Margin.Right + extra, control.Margin.Bottom);
            }
        }
        control.Tag = ControlRootTag;
        if (spec.TryGetProperty("media", out JsonElement media))
        {
            responsiveVisibility[control] = media;
            control.Visible = UiDefinition.ResolveControlVisibility(media, width);
        }
        ApplyFontSize(spec, control);
        ApplyColors(spec, control);
        ApplyBorder(spec, control);
        dom.Register(controlId, control);
        BindActions(controlId, spec, control);
        return control;
    }

    private void BindActions(string controlId, JsonElement spec, Control root)
    {
        if (!spec.TryGetProperty("actions", out JsonElement actions))
        {
            return;
        }
        foreach (JsonProperty action in actions.EnumerateObject())
        {
            switch (action.Name)
            {
                case "click":
                    ((Button)root).Click += (_, _) => actionRuntime.Invoke(controlId, "click");
                    break;
                case "change":
                    string type = UiDefinition.GetString(spec, "type");
                    if (type == "checkbox")
                    {
                        Find<CheckBox>(root).First().CheckedChanged +=
                            (_, _) => actionRuntime.Invoke(controlId, "change");
                    }
                    else if (type == "radioGroup")
                    {
                        foreach (RadioButton radio in Find<RadioButton>(root))
                        {
                            radio.CheckedChanged += (_, _) =>
                            {
                                if (radio.Checked) actionRuntime.Invoke(controlId, "change");
                            };
                        }
                    }
                    else if (type == "comboBox")
                    {
                        Find<ComboBox>(root).First().SelectedIndexChanged +=
                            (_, _) => actionRuntime.Invoke(controlId, "change");
                    }
                    else
                    {
                        Find<ListBox>(root).First().SelectedIndexChanged +=
                            (_, _) => actionRuntime.Invoke(controlId, "change");
                    }
                    break;
                case "input":
                    if (UiDefinition.GetString(spec, "type") is "textField" or "textArea")
                    {
                        Find<TextBox>(root).First().TextChanged +=
                            (_, _) => actionRuntime.Invoke(controlId, "input");
                    }
                    else
                    {
                        Find<TrackBar>(root).First().Scroll +=
                            (_, _) => actionRuntime.Invoke(controlId, "input");
                    }
                    break;
                case "enter":
                    Find<TextBox>(root).First().KeyDown += (_, eventArgs) =>
                    {
                        if (eventArgs.KeyCode == Keys.Enter)
                        {
                            eventArgs.Handled = true;
                            eventArgs.SuppressKeyPress = true;
                            actionRuntime.Invoke(controlId, "enter");
                        }
                    };
                    break;
            }
        }
    }

    private static IEnumerable<T> Find<T>(Control root) where T : Control
    {
        if (root is T matchingRoot)
        {
            yield return matchingRoot;
        }
        foreach (Control child in root.Controls)
        {
            if (child is T matching)
            {
                yield return matching;
            }
            foreach (T nested in Find<T>(child))
            {
                yield return nested;
            }
        }
    }

    private Control RenderControlContent(string controlId)
    {
        JsonElement spec = controls.GetProperty(controlId);
        string type = UiDefinition.GetString(spec, "type");
        switch (type)
        {
            case "label":
            {
                var label = new Label
                {
                    Text = UiDefinition.GetString(spec, "text"),
                    AutoSize = true,
                    MaximumSize = new Size(Math.Max(1, width), 0),
                    Margin = new Padding(3, 6, 3, 6)
                };
                wrappingLabels.Add(label);
                return label;
            }
            case "button":
            {
                string text = UiDefinition.GetString(spec, "text");
                string clickedText = UiDefinition.GetString(spec, "clickedText");
                var button = new Button
                {
                    Text = text,
                    AutoSize = true,
                    Margin = new Padding(3)
                };
                button.Click += (_, _) =>
                {
                    button.Text = clickedText;
                    status.Text = clickedText;
                };
                return button;
            }
            case "comboBox":
            {
                string label = UiDefinition.GetString(spec, "label");
                string[] options = UiDefinition.GetOptions(spec);
                var combo = new ComboBox
                {
                    DropDownStyle = ComboBoxStyle.DropDownList,
                    Width = width - 8
                };
                combo.Items.AddRange(options);
                Track(combo, -8);
                combo.SelectedIndex = UiDefinition.GetSelectedIndex(spec, options.Length);
                combo.SelectedIndexChanged += (_, _) =>
                {
                    if (combo.SelectedItem is string selected)
                    {
                        status.Text = $"{label}: {selected}";
                    }
                };
                return LabeledControl(label, combo);
            }
            case "textField":
            {
                string label = UiDefinition.GetString(spec, "label");
                bool isPassword = UiDefinition.OptionalBoolean(spec, "password");
                var textBox = new TextBox
                {
                    Text = UiDefinition.GetString(spec, "value"),
                    PlaceholderText = UiDefinition.OptionalString(spec, "placeholder"),
                    UseSystemPasswordChar = isPassword,
                    Width = width - 8
                };
                Track(textBox, -8);
                textBox.TextChanged += (_, _) =>
                {
                    status.Text = textBox.UseSystemPasswordChar
                        ? $"{label}: hidden"
                        : $"{label}: {textBox.Text}";
                };
                return LabeledControl(label, textBox);
            }
            case "textArea":
            {
                string label = UiDefinition.GetString(spec, "label");
                var textBox = new TextBox
                {
                    Multiline = true,
                    AcceptsReturn = true,
                    ScrollBars = ScrollBars.Vertical,
                    Height = 96,
                    Text = UiDefinition.GetString(spec, "value", allowEmpty: true),
                    Width = width - 8
                };
                Track(textBox, -8);
                textBox.TextChanged += (_, _) => status.Text = $"{label}: {textBox.Text}";
                return LabeledControl(label, textBox);
            }
            case "checkbox":
            {
                string label = UiDefinition.GetString(spec, "label");
                var checkBox = new CheckBox
                {
                    Text = label,
                    Checked = UiDefinition.GetBoolean(spec, "checked"),
                    AutoSize = true,
                    Margin = new Padding(3, 6, 3, 6)
                };
                checkBox.CheckedChanged += (_, _) =>
                    status.Text = $"{label}: {(checkBox.Checked ? "on" : "off")}";
                return checkBox;
            }
            case "radioGroup":
            {
                string label = UiDefinition.GetString(spec, "label");
                string[] options = UiDefinition.GetOptions(spec);
                int selectedIndex = UiDefinition.GetSelectedIndex(spec, options.Length);
                var group = new GroupBox
                {
                    Text = label,
                    Width = width,
                    Height = Math.Max(64, 34 + options.Length * 28),
                    Margin = new Padding(3, 6, 3, 6)
                };
                Track(group, 0);
                var optionList = new FlowLayoutPanel
                {
                    Dock = DockStyle.Fill,
                    FlowDirection = FlowDirection.LeftToRight,
                    WrapContents = true,
                    Padding = new Padding(6)
                };
                for (int index = 0; index < options.Length; index++)
                {
                    int optionIndex = index;
                    var radio = new RadioButton
                    {
                        Text = options[index],
                        AutoSize = true,
                        Checked = index == selectedIndex
                    };
                    radio.CheckedChanged += (_, _) =>
                    {
                        if (radio.Checked)
                        {
                            status.Text = $"{label}: {options[optionIndex]}";
                        }
                    };
                    optionList.Controls.Add(radio);
                }
                group.Controls.Add(optionList);
                return group;
            }
            case "slider":
            {
                string label = UiDefinition.GetString(spec, "label");
                int min = UiDefinition.GetInt(spec, "min");
                int max = UiDefinition.GetInt(spec, "max");
                int initialValue = UiDefinition.GetInt(spec, "value");
                var valueLabel = new Label
                {
                    Text = $"{label}: {initialValue}",
                    AutoSize = true
                };
                var trackBar = new TrackBar
                {
                    Minimum = min,
                    Maximum = max,
                    Value = initialValue,
                    TickStyle = TickStyle.None,
                    Width = width - 8
                };
                Track(trackBar, -8);
                trackBar.ValueChanged += (_, _) =>
                {
                    valueLabel.Text = $"{label}: {trackBar.Value}";
                    status.Text = valueLabel.Text;
                };
                var panel = NewControlPanel();
                panel.Controls.Add(valueLabel);
                panel.Controls.Add(trackBar);
                return panel;
            }
            case "list":
            {
                string label = UiDefinition.GetString(spec, "label");
                string[] options = UiDefinition.GetOptions(spec);
                var list = new ListBox
                {
                    Height = 96,
                    Width = width - 8
                };
                list.Items.AddRange(options);
                Track(list, -8);
                list.SelectedIndex = UiDefinition.GetSelectedIndex(spec, options.Length);
                list.SelectedIndexChanged += (_, _) =>
                {
                    if (list.SelectedItem is string selected)
                    {
                        status.Text = $"{label}: {selected}";
                    }
                };
                return LabeledControl(label, list);
            }
            case "panel":
            {
                var panel = new FlowLayoutPanel
                {
                    AutoSize = true,
                    Width = width,
                    FlowDirection = FlowDirection.TopDown,
                    WrapContents = false,
                    Margin = Padding.Empty
                };
                foreach (JsonElement child in UiDefinition.GetArray(spec, "children").EnumerateArray())
                {
                    panel.Controls.Add(Render(child.GetString()!));
                }
                Track(panel, 0);
                return panel;
            }
            case "group":
            {
                var group = new FlowLayoutPanel
                {
                    AutoSize = true,
                    AutoSizeMode = AutoSizeMode.GrowAndShrink,
                    FlowDirection = FlowDirection.TopDown,
                    WrapContents = false,
                    Margin = new Padding(3)
                };
                foreach (JsonElement child in UiDefinition.GetArray(spec, "children").EnumerateArray())
                {
                    group.Controls.Add(Render(child.GetString()!));
                }
                if (spec.TryGetProperty("layout", out JsonElement groupLayout))
                {
                    RegisterGroup(group, groupLayout);
                }
                return group;
            }
            case "scrollPanel":
            {
                string label = UiDefinition.GetString(spec, "label");
                UiDefinition.ControlDimension panelHeightSpec =
                    UiDefinition.GetDimension(spec, "height");
                int panelHeight = panelHeightSpec.Percent is null
                    ? panelHeightSpec.Pixels : 0;
                var group = new GroupBox
                {
                    Text = label,
                    Width = width,
                    Height = panelHeight + 28,
                    Margin = new Padding(3, 6, 3, 6)
                };
                var scrollPanel = new Panel
                {
                    Dock = DockStyle.Fill,
                    AutoScroll = true,
                    Padding = new Padding(6)
                };
                var children = new FlowLayoutPanel
                {
                    Dock = DockStyle.Top,
                    AutoSize = true,
                    FlowDirection = FlowDirection.TopDown,
                    WrapContents = false,
                    Width = width - 40
                };
                foreach (JsonElement child in UiDefinition.GetArray(spec, "children").EnumerateArray())
                {
                    children.Controls.Add(Render(child.GetString()!));
                }
                scrollPanel.Controls.Add(children);
                Track(group, 0);
                Track(children, -40);
                group.Controls.Add(scrollPanel);
                return group;
            }
            default:
                throw new InvalidDataException($"Unsupported control type: {type}");
        }
    }

    private Control LabeledControl(string label, Control control)
    {
        var panel = NewControlPanel();
        panel.Controls.Add(new Label
        {
            Text = label,
            AutoSize = true,
            Margin = new Padding(0, 0, 0, 3)
        });
        panel.Controls.Add(control);
        return panel;
    }

    private FlowLayoutPanel NewControlPanel()
    {
        var panel = new FlowLayoutPanel
        {
            AutoSize = true,
            Width = width,
            FlowDirection = FlowDirection.TopDown,
            WrapContents = false,
            Margin = new Padding(3, 6, 3, 6)
        };
        Track(panel, 0);
        return panel;
    }

    private void Track(Control control, int offset) => widthOffsets[control] = offset;

    internal void RegisterGroup(Control group, JsonElement layout) => groups[group] = layout;

    // Sizes controls to the available column width and picks each group's
    // direction from its media rules for the current window width.
    internal void ApplyWidth(Control root, int columnWidth, int windowWidth, int windowHeight)
    {
        width = columnWidth;
        availableHeight = windowHeight;
        ApplyColumnWidth(root, columnWidth, windowHeight, windowWidth, windowHeight);
    }

    private void ApplyColumnWidth(
        Control control, int columnWidth, int columnHeight, int windowWidth, int windowHeight)
    {
        ApplyResponsiveVisibility(control, windowWidth);
        int? maxAllowedWidth = null;
        if (maxWidths.TryGetValue(control, out int configuredMaxWidth))
        {
            maxAllowedWidth = Math.Max(
                1, Math.Min(configuredMaxWidth, columnWidth - control.Margin.Horizontal));
        }
        if (wrappingLabels.Contains(control) && control.AutoSize)
        {
            int labelMaxWidth = Math.Max(1, columnWidth - control.Margin.Horizontal);
            maxAllowedWidth = maxAllowedWidth.HasValue
                ? Math.Min(maxAllowedWidth.Value, labelMaxWidth)
                : labelMaxWidth;
        }
        if (maxAllowedWidth.HasValue)
        {
            control.MaximumSize = new Size(maxAllowedWidth.Value, 0);
        }
        if (widthOffsets.TryGetValue(control, out int offset))
        {
            control.Width = maxAllowedWidth.HasValue
                ? Math.Min(columnWidth + offset, maxAllowedWidth.Value)
                : columnWidth + offset;
        }
        if (sizes.TryGetValue(control, out ControlSizeSpec? size))
        {
            if (size.Width is UiDefinition.ControlDimension controlWidth)
            {
                int requestedWidth = controlWidth.Resolve(columnWidth);
                control.Width = maxAllowedWidth.HasValue
                    ? Math.Min(requestedWidth, maxAllowedWidth.Value)
                    : requestedWidth;
            }
            if (size.Height is UiDefinition.ControlDimension controlHeight)
            {
                int availableHeight = control.Parent?.ClientSize.Height ?? columnHeight;
                if (availableHeight <= 0) availableHeight = windowHeight;
                control.Height = controlHeight.Resolve(availableHeight) +
                    (size.ScrollPanel ? 28 : 0);
            }
            if (size.Center)
            {
                int left = size.Margin.Left;
                int right = size.Margin.Right;
                int extra = Math.Max(0, columnWidth - control.Width - left - right) / 2;
                control.Margin = new Padding(
                    left + extra, size.Margin.Top, right + extra, size.Margin.Bottom);
            }
        }
        int childWidth = control.ClientSize.Width > 0
            ? control.ClientSize.Width
            : columnWidth;
        int childHeight = control.ClientSize.Height > 0
            ? control.ClientSize.Height
            : columnHeight;
        if (control is FlowLayoutPanel flow && groups.TryGetValue(control, out JsonElement layout))
        {
            bool horizontal = UiDefinition.ResolveLayoutType(layout, windowWidth) == "horizontal";
            flow.FlowDirection = horizontal ? FlowDirection.LeftToRight : FlowDirection.TopDown;
            if (horizontal)
            {
                foreach (Control child in flow.Controls)
                {
                    ApplyResponsiveVisibility(child, windowWidth);
                }
                Control[] visibleChildren = flow.Controls
                    .Cast<Control>()
                    .Where(child => child.Visible)
                    .ToArray();
                if (visibleChildren.Length > 0)
                {
                    int availableWidth = Math.Max(
                        1,
                        childWidth - flow.Padding.Horizontal -
                            visibleChildren.Sum(child => child.Margin.Horizontal));
                    Control[] fixedWidthChildren = visibleChildren
                        .Where(maxWidths.ContainsKey)
                        .ToArray();
                    int fixedWidthBudgetPerChild = fixedWidthChildren.Length == 0
                        ? 0
                        : availableWidth / fixedWidthChildren.Length;
                    int fixedWidthTotal = fixedWidthChildren.Sum(
                        child => Math.Min(maxWidths[child], fixedWidthBudgetPerChild));
                    int flexibleCount = visibleChildren.Length - fixedWidthChildren.Length;
                    int flexibleWidth = flexibleCount == 0
                        ? 0
                        : fixedWidthChildren.Length == 0
                            ? Math.Max(80, childWidth / visibleChildren.Length - 6)
                            : Math.Max(
                                1, (availableWidth - fixedWidthTotal) / flexibleCount);
                    foreach (Control child in flow.Controls)
                    {
                        int childColumnWidth = childWidth;
                        if (child.Visible && maxWidths.TryGetValue(child, out int maxWidth))
                        {
                            childColumnWidth = Math.Min(maxWidth, fixedWidthBudgetPerChild) +
                                child.Margin.Horizontal;
                        }
                        else if (child.Visible && fixedWidthChildren.Length > 0)
                        {
                            childColumnWidth = flexibleWidth + child.Margin.Horizontal;
                        }
                        else if (child.Visible)
                        {
                            childColumnWidth = flexibleWidth +
                                (fixedWidthChildren.Length > 0 ? child.Margin.Horizontal : 0);
                        }
                        ApplyColumnWidth(
                            child, childColumnWidth, childHeight, windowWidth, windowHeight);
                    }
                    return;
                }
            }
        }
        foreach (Control child in control.Controls)
        {
            ApplyColumnWidth(child, childWidth, childHeight, windowWidth, windowHeight);
        }
    }

    private void ApplyResponsiveVisibility(Control control, int windowWidth)
    {
        if (responsiveVisibility.TryGetValue(control, out JsonElement media))
        {
            control.Visible = UiDefinition.ResolveControlVisibility(media, windowWidth);
        }
    }

    private sealed record ControlSizeSpec(
        UiDefinition.ControlDimension? Width,
        UiDefinition.ControlDimension? Height,
        bool Center,
        Padding Margin,
        bool ScrollPanel);

    private void ApplyColors(JsonElement spec, Control control)
    {
        Color? background = UiDefinition.GetBackgroundColor(spec) ?? themeBackground;
        Color? foreground = UiDefinition.GetForegroundColor(spec) ?? themeForeground;
        if (foreground == null && background != null)
        {
            foreground = UiDefinition.ContrastForeground(background.Value);
        }
        if (background != null)
        {
            ApplyBackgroundColor(control, background.Value);
        }
        if (foreground != null)
        {
            ApplyForegroundColor(control, foreground.Value);
        }
    }

    private static void ApplyFontSize(JsonElement spec, Control control)
    {
        if (!spec.TryGetProperty("fontSize", out _))
        {
            return;
        }
        float sizeInPoints = UiDefinition.GetPositiveInt(spec, "fontSize") * 72f / 96f;
        ApplyFontSize(control, sizeInPoints);
    }

    private static void ApplyFontSize(Control control, float sizeInPoints)
    {
        control.Font = new Font(
            control.Font.FontFamily,
            sizeInPoints,
            control.Font.Style,
            GraphicsUnit.Point);
        foreach (Control child in control.Controls)
        {
            if (!ReferenceEquals(child.Tag, ControlRootTag))
            {
                ApplyFontSize(child, sizeInPoints);
            }
        }
    }

    private static void ApplyBorder(JsonElement spec, Control control)
    {
        if (!spec.TryGetProperty("borderStyle", out JsonElement styleValue) &&
            !spec.TryGetProperty("borderColor", out _) &&
            !spec.TryGetProperty("borderWidth", out _))
        {
            return;
        }

        string style = styleValue.ValueKind == JsonValueKind.String
            ? styleValue.GetString()!
            : "solid";
        if (style == "none")
        {
            return;
        }
        int width = spec.TryGetProperty("borderWidth", out _)
            ? UiDefinition.GetNonNegativeInt(spec, "borderWidth")
            : 1;
        if (width == 0)
        {
            return;
        }
        Color color = UiDefinition.GetBorderColor(spec) ?? Color.Black;
        DashStyle dashStyle = style switch
        {
            "dashed" => DashStyle.Dash,
            "dotted" => DashStyle.Dot,
            _ => DashStyle.Solid
        };
        if (control is Button button && dashStyle == DashStyle.Solid)
        {
            button.FlatStyle = FlatStyle.Flat;
            button.FlatAppearance.BorderColor = color;
            button.FlatAppearance.BorderSize = width;
            return;
        }

        control.Paint += (_, eventArgs) =>
        {
            Rectangle bounds = control.ClientRectangle;
            if (bounds.Width <= width || bounds.Height <= width)
            {
                return;
            }
            using var pen = new Pen(color, width) { DashStyle = dashStyle };
            int inset = width / 2;
            eventArgs.Graphics.DrawRectangle(
                pen, inset, inset, bounds.Width - width, bounds.Height - width);
        };
    }

    private static void ApplyBackgroundColor(Control control, Color color)
    {
        if (control is ButtonBase button)
        {
            button.UseVisualStyleBackColor = false;
        }
        control.BackColor = color;
        foreach (Control child in control.Controls)
        {
            if (!ReferenceEquals(child.Tag, ControlRootTag))
            {
                ApplyBackgroundColor(child, color);
            }
        }
    }

    private static void ApplyForegroundColor(Control control, Color color)
    {
        control.ForeColor = color;
        foreach (Control child in control.Controls)
        {
            if (!ReferenceEquals(child.Tag, ControlRootTag))
            {
                ApplyForegroundColor(child, color);
            }
        }
    }
}
