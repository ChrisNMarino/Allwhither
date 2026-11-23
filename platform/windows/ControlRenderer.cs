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
    private readonly Dictionary<Control, int> widthOffsets = new();
    private readonly Dictionary<Control, JsonElement> groups = new();
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
        control.Tag = ControlRootTag;
        ApplyColors(spec, control);
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
                return new Label
                {
                    Text = UiDefinition.GetString(spec, "text"),
                    AutoSize = true,
                    Margin = new Padding(3, 6, 3, 6)
                };
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
                string label = UiDefinition.GetString(spec, "label");
                var panel = new FlowLayoutPanel
                {
                    AutoSize = true,
                    Width = width,
                    FlowDirection = FlowDirection.TopDown,
                    WrapContents = false,
                    Margin = new Padding(3, 6, 3, 6)
                };
                panel.Controls.Add(new Label
                {
                    Text = label,
                    AutoSize = true,
                    Font = new Font(SystemFonts.DefaultFont.FontFamily, 12, FontStyle.Bold),
                    Margin = new Padding(0, 0, 0, 8)
                });
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
                int panelHeight = UiDefinition.GetPositiveInt(spec, "height");
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
    internal void ApplyWidth(Control root, int columnWidth, int windowWidth)
    {
        width = columnWidth;
        ApplyColumnWidth(root, columnWidth, windowWidth);
    }

    private void ApplyColumnWidth(Control control, int columnWidth, int windowWidth)
    {
        if (widthOffsets.TryGetValue(control, out int offset))
        {
            control.Width = columnWidth + offset;
        }
        int childWidth = columnWidth;
        if (control is FlowLayoutPanel flow && groups.TryGetValue(control, out JsonElement layout))
        {
            bool horizontal = UiDefinition.ResolveLayoutType(layout, windowWidth) == "horizontal";
            flow.FlowDirection = horizontal ? FlowDirection.LeftToRight : FlowDirection.TopDown;
            if (horizontal && flow.Controls.Count > 0)
            {
                childWidth = Math.Max(80, columnWidth / flow.Controls.Count - 6);
            }
        }
        foreach (Control child in control.Controls)
        {
            ApplyColumnWidth(child, childWidth, windowWidth);
        }
    }

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
