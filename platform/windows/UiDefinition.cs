using System.Text.Json;
using System.Globalization;

namespace Allwhither.Windows;

internal static class UiDefinition
{
    internal static void ValidateMenus(JsonElement menus)
    {
        foreach (JsonElement menu in menus.EnumerateArray())
        {
            GetString(menu, "label");
            JsonElement items = GetArray(menu, "items");
            if (items.GetArrayLength() == 0)
            {
                throw new InvalidDataException("Menus must contain at least one item.");
            }
            foreach (JsonElement item in items.EnumerateArray())
            {
                GetString(item, "text");
                GetString(item, "message");
            }
        }
    }

    internal static void ValidateControlTree(JsonElement controls, JsonElement order)
    {
        var visited = new HashSet<string>(StringComparer.Ordinal);
        var visiting = new HashSet<string>(StringComparer.Ordinal);
        foreach (JsonElement root in order.EnumerateArray())
        {
            VisitControl(GetControlId(root), controls, visited, visiting);
        }
        foreach (JsonProperty control in controls.EnumerateObject())
        {
            if (!visited.Contains(control.Name) &&
                GetString(control.Value, "type") == "panel")
            {
                VisitControl(control.Name, controls, visited, visiting);
            }
        }
        foreach (JsonProperty control in controls.EnumerateObject())
        {
            if (!visited.Contains(control.Name))
            {
                throw new InvalidDataException(
                    $"Control '{control.Name}' is not reachable from window.layout.children.");
            }
        }
    }

    private static void VisitControl(
        string id, JsonElement controls, HashSet<string> visited, HashSet<string> visiting)
    {
        if (!controls.TryGetProperty(id, out JsonElement spec))
        {
            throw new InvalidDataException($"Unknown control id: {id}");
        }
        if (visiting.Contains(id))
        {
            throw new InvalidDataException($"Control hierarchy contains a cycle at: {id}");
        }
        if (!visited.Add(id))
        {
            throw new InvalidDataException($"Control id is referenced more than once: {id}");
        }

        visiting.Add(id);
        ValidateControl(spec);
        if (GetString(spec, "type") is "scrollPanel" or "panel" or "group")
        {
            foreach (JsonElement child in GetArray(spec, "children").EnumerateArray())
            {
                VisitControl(GetControlId(child), controls, visited, visiting);
            }
        }
        visiting.Remove(id);
    }

    private static string GetControlId(JsonElement value)
    {
        if (value.ValueKind != JsonValueKind.String ||
            string.IsNullOrWhiteSpace(value.GetString()))
        {
            throw new InvalidDataException("Control references must be non-empty strings.");
        }
        return value.GetString()!;
    }

    private static void ValidateControl(JsonElement spec)
    {
        GetBackgroundColor(spec);
        GetForegroundColor(spec);
        GetBorderColor(spec);
        if (spec.TryGetProperty("borderStyle", out JsonElement borderStyle) &&
            (borderStyle.ValueKind != JsonValueKind.String ||
             borderStyle.GetString() is not ("solid" or "dashed" or "dotted" or "none")))
        {
            throw new InvalidDataException(
                "borderStyle must be solid, dashed, dotted, or none.");
        }
        if (spec.TryGetProperty("borderWidth", out _))
        {
            GetNonNegativeInt(spec, "borderWidth");
        }
        if (spec.TryGetProperty("fontSize", out _))
        {
            GetPositiveInt(spec, "fontSize");
        }
        if (spec.TryGetProperty("maxWidth", out _))
        {
            GetPositiveInt(spec, "maxWidth");
        }
        if (spec.TryGetProperty("media", out JsonElement controlMedia))
        {
            ValidateControlMedia(controlMedia);
        }
        ValidateActions(spec);
        foreach (string dimension in new[] { "width", "height" })
        {
            if (spec.TryGetProperty(dimension, out _))
            {
                GetDimension(spec, dimension);
            }
        }
        if (spec.TryGetProperty("center", out JsonElement center) &&
            center.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
        {
            throw new InvalidDataException("center must be a boolean.");
        }
        switch (GetString(spec, "type"))
        {
            case "label":
                GetString(spec, "text");
                break;
            case "button":
                GetString(spec, "text");
                GetString(spec, "clickedText");
                break;
            case "comboBox":
            case "radioGroup":
            case "list":
                GetString(spec, "label");
                GetSelectedIndex(spec, GetOptions(spec).Length);
                break;
            case "textField":
                GetString(spec, "label");
                GetString(spec, "value", allowEmpty: true);
                if (spec.TryGetProperty("placeholder", out JsonElement placeholder) &&
                    placeholder.ValueKind != JsonValueKind.String)
                {
                    throw new InvalidDataException("textField.placeholder must be a string.");
                }
                if (spec.TryGetProperty("autocomplete", out JsonElement autocomplete) &&
                    autocomplete.ValueKind != JsonValueKind.String)
                {
                    throw new InvalidDataException("textField.autocomplete must be a string.");
                }
                break;
            case "textArea":
                GetString(spec, "label");
                GetString(spec, "value", allowEmpty: true);
                break;
            case "checkbox":
                GetString(spec, "label");
                GetBoolean(spec, "checked");
                break;
            case "slider":
            {
                GetString(spec, "label");
                int min = GetInt(spec, "min");
                int max = GetInt(spec, "max");
                int value = GetInt(spec, "value");
                if (min >= max || value < min || value > max)
                {
                    throw new InvalidDataException("Slider range or value is invalid.");
                }
                break;
            }
            case "scrollPanel":
                GetString(spec, "label");
                GetDimension(spec, "height");
                foreach (JsonElement child in GetArray(spec, "children").EnumerateArray())
                {
                    GetControlId(child);
                }
                break;
            case "panel":
                foreach (JsonElement child in GetArray(spec, "children").EnumerateArray())
                {
                    GetControlId(child);
                }
                break;
            case "group":
                if (spec.TryGetProperty("layout", out JsonElement groupLayout))
                {
                    ValidateLayout(groupLayout, false, "group.layout");
                }
                foreach (JsonElement child in GetArray(spec, "children").EnumerateArray())
                {
                    GetControlId(child);
                }
                break;
            default:
                throw new InvalidDataException("Unsupported control type.");
        }
    }

    internal static void ValidateLayout(JsonElement layout, bool allowCenter, string name)
    {
        if (layout.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidDataException($"{name} must be an object.");
        }
        string type = GetString(layout, "type");
        if (type is not ("vertical" or "horizontal") && !(allowCenter && type == "center"))
        {
            throw new InvalidDataException($"{name}.type is not supported.");
        }
        if (!layout.TryGetProperty("media", out JsonElement media))
        {
            return;
        }
        if (type == "center" || media.ValueKind != JsonValueKind.Array)
        {
            throw new InvalidDataException(
                $"{name}.media must be an array and is not supported for center layouts.");
        }
        foreach (JsonElement rule in media.EnumerateArray())
        {
            if (rule.ValueKind != JsonValueKind.Object ||
                GetString(rule, "type") is not ("vertical" or "horizontal"))
            {
                throw new InvalidDataException($"{name}.media type must be 'vertical' or 'horizontal'.");
            }
            bool hasMin = rule.TryGetProperty("minWidth", out _);
            bool hasMax = rule.TryGetProperty("maxWidth", out _);
            if (!hasMin && !hasMax)
            {
                throw new InvalidDataException($"{name}.media rules need minWidth or maxWidth.");
            }
            if ((hasMin && GetInt(rule, "minWidth") < 0) || (hasMax && GetInt(rule, "maxWidth") < 0))
            {
                throw new InvalidDataException($"{name}.media widths must not be negative.");
            }
        }
    }

    internal static string ResolveLayoutType(JsonElement layout, int width)
    {
        string type = GetString(layout, "type");
        if (!layout.TryGetProperty("media", out JsonElement media))
        {
            return type;
        }
        foreach (JsonElement rule in media.EnumerateArray())
        {
            if ((!rule.TryGetProperty("minWidth", out JsonElement min) || width >= min.GetInt32()) &&
                (!rule.TryGetProperty("maxWidth", out JsonElement max) || width <= max.GetInt32()))
            {
                type = GetString(rule, "type");
            }
        }
        return type;
    }

    private static void ValidateControlMedia(JsonElement media)
    {
        if (media.ValueKind != JsonValueKind.Array)
        {
            throw new InvalidDataException("control.media must be an array.");
        }
        foreach (JsonElement rule in media.EnumerateArray())
        {
            if (rule.ValueKind != JsonValueKind.Object)
            {
                throw new InvalidDataException("control.media rules must be objects.");
            }
            bool hasMin = rule.TryGetProperty("minWidth", out _);
            bool hasMax = rule.TryGetProperty("maxWidth", out _);
            if (!hasMin && !hasMax)
            {
                throw new InvalidDataException("control.media rules need minWidth or maxWidth.");
            }
            if ((hasMin && GetInt(rule, "minWidth") < 0) ||
                (hasMax && GetInt(rule, "maxWidth") < 0))
            {
                throw new InvalidDataException("control.media widths must not be negative.");
            }
            if (!rule.TryGetProperty("visible", out JsonElement visible) ||
                visible.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
            {
                throw new InvalidDataException("control.media visible must be a boolean.");
            }
        }
    }

    internal static bool ResolveControlVisibility(JsonElement media, int width)
    {
        bool visible = true;
        foreach (JsonElement rule in media.EnumerateArray())
        {
            if ((!rule.TryGetProperty("minWidth", out JsonElement min) ||
                 width >= min.GetInt32()) &&
                (!rule.TryGetProperty("maxWidth", out JsonElement max) ||
                 width <= max.GetInt32()))
            {
                visible = rule.GetProperty("visible").GetBoolean();
            }
        }
        return visible;
    }

    private static void ValidateActions(JsonElement spec)
    {
        if (!spec.TryGetProperty("actions", out JsonElement actions))
        {
            return;
        }
        if (actions.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidDataException("control.actions must be an object.");
        }
        string type = GetString(spec, "type");
        string[] supportedEvents = type switch
        {
            "button" => ["click"],
            "comboBox" or "checkbox" or "radioGroup" or "list" => ["change"],
            "textField" => ["input", "enter"],
            "textArea" or "slider" => ["input"],
            _ => []
        };
        foreach (JsonProperty action in actions.EnumerateObject())
        {
            if (!supportedEvents.Contains(action.Name))
            {
                throw new InvalidDataException(
                    $"{type} does not support the '{action.Name}' action event.");
            }
            if (action.Value.ValueKind != JsonValueKind.Object)
            {
                throw new InvalidDataException(
                    $"{action.Name} action must specify a file and function.");
            }
            string file = GetString(action.Value, "file");
            string[] segments = file.Replace('\\', '/').Split('/');
            if (!file.EndsWith(".js", StringComparison.OrdinalIgnoreCase) ||
                Path.IsPathRooted(file) || segments.Contains(".."))
            {
                throw new InvalidDataException(
                    "Action files must be relative .js paths without parent segments.");
            }
            string function = GetString(action.Value, "function");
            if (!IsJavaScriptIdentifier(function))
            {
                throw new InvalidDataException("Action function must be a JavaScript identifier.");
            }
        }
    }

    private static bool IsJavaScriptIdentifier(string value)
    {
        static bool IsStart(char character) =>
            char.IsLetter(character) || character is '_' or '$';
        static bool IsPart(char character) =>
            IsStart(character) || char.IsDigit(character);

        return value.Length > 0 && IsStart(value[0]) && value.Skip(1).All(IsPart);
    }

    internal readonly struct ThemeColors
    {
        internal Color? Background { get; init; }
        internal Color? Foreground { get; init; }
    }

    internal static Color? GetBackgroundColor(JsonElement spec) =>
        OptionalColor(spec, "backgroundColor", "control.backgroundColor");

    internal static Color? GetForegroundColor(JsonElement spec) =>
        OptionalColor(spec, "foregroundColor", "control.foregroundColor");

    internal static Color? GetBorderColor(JsonElement spec) =>
        OptionalColor(spec, "borderColor", "control.borderColor");

    internal static ThemeColors GetTheme(JsonElement ui)
    {
        if (!ui.TryGetProperty("theme", out JsonElement theme))
        {
            return new ThemeColors();
        }
        if (theme.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidDataException("theme must be an object.");
        }
        return new ThemeColors
        {
            Background = OptionalColor(theme, "backgroundColor", "theme.backgroundColor"),
            Foreground = OptionalColor(theme, "foregroundColor", "theme.foregroundColor")
        };
    }

    internal static ThemeColors GetChromeColors(JsonElement window)
    {
        Color? background = OptionalColor(window, "backgroundColor", "window.backgroundColor");
        Color? foreground = OptionalColor(window, "foregroundColor", "window.foregroundColor");
        if (foreground == null && background != null)
        {
            foreground = ContrastForeground(background.Value);
        }
        return new ThemeColors { Background = background, Foreground = foreground };
    }

    internal static Color ContrastForeground(Color background)
    {
        double luminance = RelativeLuminance(background);
        double black = (luminance + 0.05) / 0.05;
        double white = 1.05 / (luminance + 0.05);
        return black >= white ? Color.Black : Color.White;
    }

    private static Color? OptionalColor(JsonElement parent, string name, string label)
    {
        if (!parent.TryGetProperty(name, out JsonElement value))
        {
            return null;
        }
        if (value.ValueKind != JsonValueKind.String)
        {
            throw new InvalidDataException($"{label} must be a #RRGGBB color.");
        }
        string color = value.GetString()!;
        if (color.Length != 7 || color[0] != '#' ||
            color.Skip(1).Any(character => !Uri.IsHexDigit(character)))
        {
            throw new InvalidDataException($"{label} must be a #RRGGBB color.");
        }
        int rgb = Convert.ToInt32(color.Substring(1), 16);
        return Color.FromArgb((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    }

    private static double RelativeLuminance(Color color)
    {
        static double Linearize(int channel)
        {
            double value = channel / 255.0;
            return value <= 0.04045
                ? value / 12.92
                : Math.Pow((value + 0.055) / 1.055, 2.4);
        }
        return 0.2126 * Linearize(color.R) +
            0.7152 * Linearize(color.G) +
            0.0722 * Linearize(color.B);
    }

    internal static string[] GetOptions(JsonElement spec)
    {
        JsonElement options = GetArray(spec, "options");
        string[] values = options.EnumerateArray()
            .Select((option, index) => option.ValueKind == JsonValueKind.String &&
                !string.IsNullOrWhiteSpace(option.GetString())
                    ? option.GetString()!
                    : throw new InvalidDataException($"options[{index}] must be a non-empty string."))
            .ToArray();
        if (values.Length == 0)
        {
            throw new InvalidDataException("At least one option is required.");
        }
        return values;
    }

    internal static int GetSelectedIndex(JsonElement spec, int optionCount)
    {
        int index = GetInt(spec, "selectedIndex");
        if (index < 0 || index >= optionCount)
        {
            throw new InvalidDataException("selectedIndex is out of range.");
        }
        return index;
    }

    internal static JsonElement GetObject(JsonElement parent, string name)
    {
        if (!parent.TryGetProperty(name, out JsonElement value) ||
            value.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidDataException($"{name} must be an object.");
        }
        return value;
    }

    internal static JsonElement GetArray(JsonElement parent, string name)
    {
        if (!parent.TryGetProperty(name, out JsonElement value) ||
            value.ValueKind != JsonValueKind.Array)
        {
            throw new InvalidDataException($"{name} must be an array.");
        }
        return value;
    }

    internal static string GetString(JsonElement parent, string name, bool allowEmpty = false)
    {
        if (!parent.TryGetProperty(name, out JsonElement value) ||
            value.ValueKind != JsonValueKind.String)
        {
            throw new InvalidDataException($"{name} must be a string.");
        }
        string text = value.GetString()!;
        if (!allowEmpty && string.IsNullOrWhiteSpace(text))
        {
            throw new InvalidDataException($"{name} must not be empty.");
        }
        return text;
    }

    internal static int GetInt(JsonElement parent, string name)
    {
        if (!parent.TryGetProperty(name, out JsonElement value) ||
            value.ValueKind != JsonValueKind.Number ||
            !value.TryGetInt32(out int number))
        {
            throw new InvalidDataException($"{name} must be an integer.");
        }
        return number;
    }

    internal static int GetPositiveInt(JsonElement parent, string name)
    {
        int value = GetInt(parent, name);
        if (value <= 0)
        {
            throw new InvalidDataException($"{name} must be positive.");
        }
        return value;
    }

    internal static int GetNonNegativeInt(JsonElement parent, string name)
    {
        int value = GetInt(parent, name);
        if (value < 0)
        {
            throw new InvalidDataException($"{name} must be non-negative.");
        }
        return value;
    }

    internal static ControlDimension GetDimension(JsonElement parent, string name)
    {
        if (!parent.TryGetProperty(name, out JsonElement value))
        {
            throw new InvalidDataException($"{name} must be a positive pixel value or percentage.");
        }
        if (value.ValueKind == JsonValueKind.Number &&
            value.TryGetInt32(out int pixels) && pixels > 0)
        {
            return new ControlDimension(pixels, null);
        }
        if (value.ValueKind == JsonValueKind.String)
        {
            string text = value.GetString()!;
            if (System.Text.RegularExpressions.Regex.IsMatch(
                    text, @"^\d+(?:\.\d+)?%$") &&
                double.TryParse(text[..^1], NumberStyles.AllowDecimalPoint,
                    CultureInfo.InvariantCulture, out double percent) &&
                percent > 0 && percent <= 100)
            {
                return new ControlDimension(0, percent);
            }
        }
        throw new InvalidDataException(
            $"{name} must be a positive pixel value or a percentage greater than 0% and at most 100%.");
    }

    internal readonly record struct ControlDimension(int Pixels, double? Percent)
    {
        internal int Resolve(int available) =>
            Percent is double percent
                ? (int)Math.Round(available * percent / 100, MidpointRounding.AwayFromZero)
                : Pixels;
    }

    internal static bool GetBoolean(JsonElement parent, string name)
    {
        if (!parent.TryGetProperty(name, out JsonElement value) ||
            value.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
        {
            throw new InvalidDataException($"{name} must be a boolean.");
        }
        return value.GetBoolean();
    }

    internal static bool OptionalBoolean(JsonElement parent, string name)
    {
        if (!parent.TryGetProperty(name, out JsonElement value))
        {
            return false;
        }
        if (value.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
        {
            throw new InvalidDataException($"{name} must be a boolean.");
        }
        return value.GetBoolean();
    }

    internal static string OptionalString(JsonElement parent, string name) =>
        parent.TryGetProperty(name, out JsonElement value) &&
        value.ValueKind == JsonValueKind.String
            ? value.GetString()!
            : "";
}
