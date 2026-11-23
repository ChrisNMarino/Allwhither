using System.Text.Json;
using System.Windows.Forms;

namespace Omni.Windows;

public sealed class UiDom
{
    private readonly JsonElement definitions;
    private readonly ToolStripStatusLabel status;
    private readonly Dictionary<string, Control> elements = new(StringComparer.Ordinal);
    private Control? mountParent;
    private Func<string, Control>? renderControl;

    internal UiDom(JsonElement definitions, ToolStripStatusLabel status)
    {
        this.definitions = definitions;
        this.status = status;
    }

    internal void Register(string id, Control control) => elements[id] = control;

    internal void AttachMount(Control parent, Func<string, Control> render)
    {
        mountParent = parent;
        renderControl = render;
    }

    public string GetText(string id)
    {
        JsonElement definition = Definition(id);
        Control control = Element(id);
        string type = UiDefinition.GetString(definition, "type");
        if (type is "label" or "button" or "checkbox" or "radioGroup" or "scrollPanel")
        {
            return control.Text;
        }
        return Find<Label>(control)?.Text ??
            (definition.TryGetProperty("label", out JsonElement label)
                ? label.GetString()!
                : "");
    }

    public string getText(string id) => GetText(id);

    public void SetText(string id, string text)
    {
        string type = UiDefinition.GetString(Definition(id), "type");
        Control control = Element(id);
        if (type is "label" or "button" or "checkbox")
        {
            control.Text = text;
            return;
        }
        if ((type is "radioGroup" or "scrollPanel") && control is GroupBox group)
        {
            group.Text = text;
            return;
        }
        Label? caption = Find<Label>(control);
        if (caption is null)
        {
            throw new InvalidDataException($"Control '{id}' does not expose writable text.");
        }
        caption.Text = text;
    }

    public void setText(string id, string text) => SetText(id, text);

    public string? GetValue(string id)
    {
        Control control = Element(id);
        return control switch
        {
            TextBox textBox => textBox.Text,
            _ when Find<TextBox>(control) is TextBox nestedTextBox => nestedTextBox.Text,
            _ when Find<ComboBox>(control) is ComboBox combo =>
                combo.SelectedItem?.ToString(),
            _ when Find<ListBox>(control) is ListBox list =>
                list.SelectedItem?.ToString(),
            _ when Find<TrackBar>(control) is TrackBar slider =>
                slider.Value.ToString(),
            _ when FindSelectedRadio(control) is RadioButton radio => radio.Text,
            _ => null
        };
    }

    public string? getValue(string id) => GetValue(id);

    public void SetValue(string id, string value)
    {
        Control control = Element(id);
        if (control is TextBox textBox)
        {
            textBox.Text = value;
            return;
        }
        if (Find<ComboBox>(control) is ComboBox combo)
        {
            if (!combo.Items.Contains(value))
            {
                throw new InvalidDataException($"Value '{value}' is not an option of control '{id}'.");
            }
            combo.SelectedItem = value;
            return;
        }
        if (Find<ListBox>(control) is ListBox list)
        {
            if (!list.Items.Contains(value))
            {
                throw new InvalidDataException($"Value '{value}' is not an option of control '{id}'.");
            }
            list.SelectedItem = value;
            return;
        }
        if (Find<TrackBar>(control) is TrackBar slider &&
            int.TryParse(value, out int sliderValue))
        {
            slider.Value = Math.Clamp(sliderValue, slider.Minimum, slider.Maximum);
            return;
        }
        RadioButton? radio = Find<RadioButton>(control.Controls)
            .FirstOrDefault(option => option.Text == value);
        if (radio is not null)
        {
            radio.Checked = true;
            return;
        }
        throw new InvalidDataException($"Control '{id}' does not expose a writable value.");
    }

    public void setValue(string id, string value) => SetValue(id, value);

    public bool GetChecked(string id)
    {
        if (Find<CheckBox>(Element(id)) is CheckBox checkBox)
        {
            return checkBox.Checked;
        }
        throw new InvalidDataException($"Control '{id}' does not expose a checked state.");
    }

    public bool getChecked(string id) => GetChecked(id);

    public void SetChecked(string id, bool checkedValue)
    {
        if (Find<CheckBox>(Element(id)) is CheckBox checkBox)
        {
            checkBox.Checked = checkedValue;
            return;
        }
        throw new InvalidDataException($"Control '{id}' does not expose a checked state.");
    }

    public void setChecked(string id, bool checkedValue) => SetChecked(id, checkedValue);

    public void SetSecure(string id, bool secure)
    {
        if (Find<TextBox>(Element(id)) is TextBox textBox)
        {
            textBox.UseSystemPasswordChar = secure;
            return;
        }
        throw new InvalidDataException($"Control '{id}' does not expose a secure state.");
    }

    public void setSecure(string id, bool secure) => SetSecure(id, secure);

    public bool GetSecure(string id)
    {
        if (Find<TextBox>(Element(id)) is TextBox textBox)
        {
            return textBox.UseSystemPasswordChar;
        }
        return false;
    }

    public bool getSecure(string id) => GetSecure(id);

    public void SetStatus(string text) => status.Text = text;
    public void setStatus(string text) => SetStatus(text);

    public void AddPanel(string id) => addPanel(id);

    public void addPanel(string id)
    {
        if (UiDefinition.GetString(Definition(id), "type") != "panel")
        {
            throw new InvalidDataException($"Control '{id}' is not a panel.");
        }
        if (elements.ContainsKey(id))
        {
            throw new InvalidDataException($"Panel '{id}' is already shown.");
        }
        if (mountParent is null || renderControl is null)
        {
            throw new InvalidDataException("The window is not ready to add a panel.");
        }
        mountParent.Controls.Add(renderControl(id));
    }

    public void RemovePanel(string id) => removePanel(id);

    public void removePanel(string id)
    {
        if (UiDefinition.GetString(Definition(id), "type") != "panel")
        {
            throw new InvalidDataException($"Control '{id}' is not a panel.");
        }
        if (!elements.TryGetValue(id, out Control? control))
        {
            throw new InvalidDataException($"Panel '{id}' is not shown.");
        }
        UnregisterTree(id);
        control.Parent?.Controls.Remove(control);
        control.Dispose();
    }

    public void setOptions(string id, string[] options) => SetOptions(id, options, null);

    public void setOptions(string id, string[] options, int selectedIndex) =>
        SetOptions(id, options, selectedIndex);

    public void SetOptions(string id, string[] options, int? selectedIndex)
    {
        string type = UiDefinition.GetString(Definition(id), "type");
        if (type is not ("list" or "comboBox"))
        {
            throw new InvalidDataException($"Control '{id}' does not expose options.");
        }
        if (options.Any(option => string.IsNullOrWhiteSpace(option)))
        {
            throw new InvalidDataException("options must be non-empty strings.");
        }
        int index;
        if (options.Length == 0)
        {
            if (selectedIndex is int chosen && chosen != -1)
            {
                throw new InvalidDataException("selectedIndex is out of range.");
            }
            index = -1;
        }
        else if (selectedIndex is int chosen)
        {
            if (chosen < 0 || chosen >= options.Length)
            {
                throw new InvalidDataException("selectedIndex is out of range.");
            }
            index = chosen;
        }
        else
        {
            index = 0;
        }
        Control control = Element(id);
        if (type == "list")
        {
            if (Find<ListBox>(control) is not ListBox list)
            {
                throw new InvalidDataException($"Control '{id}' does not expose options.");
            }
            list.Items.Clear();
            list.Items.AddRange(options);
            list.SelectedIndex = index;
            return;
        }
        if (Find<ComboBox>(control) is not ComboBox combo)
        {
            throw new InvalidDataException($"Control '{id}' does not expose options.");
        }
        combo.Items.Clear();
        combo.Items.AddRange(options);
        combo.SelectedIndex = index;
    }

    public int GetSelectedIndex(string id) => getSelectedIndex(id);

    public int getSelectedIndex(string id)
    {
        string type = UiDefinition.GetString(Definition(id), "type");
        Control control = Element(id);
        if (type == "list" && Find<ListBox>(control) is ListBox list)
        {
            return list.SelectedIndex;
        }
        if (type == "comboBox" && Find<ComboBox>(control) is ComboBox combo)
        {
            return combo.SelectedIndex;
        }
        throw new InvalidDataException($"Control '{id}' does not expose a selection index.");
    }

    private void UnregisterTree(string id)
    {
        if (definitions.TryGetProperty(id, out JsonElement definition))
        {
            string type = UiDefinition.GetString(definition, "type");
            if (type is "panel" or "scrollPanel")
            {
                foreach (JsonElement child in UiDefinition.GetArray(definition, "children").EnumerateArray())
                {
                    UnregisterTree(child.GetString()!);
                }
            }
        }
        elements.Remove(id);
    }

    internal JsonElement Definition(string id)
    {
        if (!definitions.TryGetProperty(id, out JsonElement definition))
        {
            throw new InvalidDataException($"Unknown control id: {id}");
        }
        return definition;
    }

    internal Control Element(string id) =>
        elements.TryGetValue(id, out Control? control)
            ? control
            : throw new InvalidDataException($"Control '{id}' has not been rendered.");

    private static T? Find<T>(Control control) where T : Control =>
        control is T matching
            ? matching
            : Find<T>(control.Controls).FirstOrDefault();

    private static IEnumerable<T> Find<T>(Control.ControlCollection controls)
        where T : Control
    {
        foreach (Control control in controls)
        {
            if (control is T matching)
            {
                yield return matching;
            }
            foreach (T nested in Find<T>(control.Controls))
            {
                yield return nested;
            }
        }
    }

    private static RadioButton? FindSelectedRadio(Control control) =>
        Find<RadioButton>(control.Controls).FirstOrDefault(radio => radio.Checked);
}
