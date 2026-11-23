using System.Text.Json;
using Jint;
using Jint.Runtime;

namespace Omni.Windows;

public sealed class JavaScriptActionEvent(
    string type,
    string targetId,
    string? text,
    string? value,
    bool? checkedValue)
{
    public string type { get; } = type;
    public string targetId { get; } = targetId;
    public string? text { get; } = text;
    public string? value { get; } = value;
    public bool? @checked { get; } = checkedValue;
}

internal sealed class JavaScriptActionRuntime
{
    private readonly JsonElement controls;
    private readonly UiDom dom;
    private readonly Engine engine;
    private readonly JsonElement? scripts;
    private readonly HashSet<string> loadedFiles = new(StringComparer.Ordinal);
    private bool invoking;

    internal JavaScriptActionRuntime(JsonElement controls, UiDom dom, JsonElement? scripts = null)
    {
        this.controls = controls;
        this.dom = dom;
        this.scripts = scripts;
        engine = new Engine();
        engine.SetValue("dom", dom).SetValue("api", new NativeApi(engine));
    }

    internal void Prepare()
    {
        if (scripts is JsonElement files)
        {
            if (files.ValueKind != JsonValueKind.Array)
            {
                throw new InvalidDataException("scripts must be an array.");
            }
            foreach (JsonElement file in files.EnumerateArray())
            {
                if (file.ValueKind != JsonValueKind.String ||
                    string.IsNullOrWhiteSpace(file.GetString()))
                {
                    throw new InvalidDataException("scripts must contain non-empty paths.");
                }
                LoadScript(file.GetString()!, action: false);
            }
        }
        foreach (JsonProperty control in controls.EnumerateObject())
        {
            if (!control.Value.TryGetProperty("actions", out JsonElement actions))
            {
                continue;
            }
            foreach (JsonProperty action in actions.EnumerateObject())
            {
                LoadAction(UiDefinition.GetString(action.Value, "file"),
                    UiDefinition.GetString(action.Value, "function"));
            }
        }
    }

    internal void Invoke(string controlId, string eventType)
    {
        if (invoking)
        {
            return;
        }
        JsonElement definition = dom.Definition(controlId);
        if (!definition.TryGetProperty("actions", out JsonElement actions) ||
            !actions.TryGetProperty(eventType, out JsonElement action))
        {
            return;
        }
        string file = UiDefinition.GetString(action, "file");
        string function = UiDefinition.GetString(action, "function");
        LoadAction(file, function);
        var eventData = new JavaScriptActionEvent(
            eventType,
            controlId,
            dom.GetText(controlId),
            dom.GetValue(controlId),
            UiDefinition.GetString(definition, "type") == "checkbox"
                ? dom.GetChecked(controlId)
                : null);
        try
        {
            invoking = true;
            engine.Invoke(function, dom, eventData);
        }
        catch (JavaScriptException exception)
        {
            dom.SetStatus($"JavaScript action failed: {exception.Message}");
        }
        finally
        {
            invoking = false;
        }
    }

    private void LoadAction(string file, string function)
    {
        LoadScript(file, action: true);
        if (!engine.Evaluate($"typeof {function} === 'function'").AsBoolean())
        {
            throw new InvalidDataException(
                $"Action function '{function}' was not defined by {file}.");
        }
    }

    private void LoadScript(string file, bool action)
    {
        string resourcePath = EmbeddedResources.Normalize(file);
        if (!resourcePath.EndsWith(".js", StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException(action
                ? "Action files must be relative .js paths without parent segments."
                : "Script files must be relative .js paths without parent segments.");
        }
        if (loadedFiles.Add(resourcePath))
        {
            try
            {
                engine.Execute(EmbeddedResources.ReadText(resourcePath));
            }
            catch (JavaScriptException exception)
            {
                throw new InvalidDataException(
                    $"Could not evaluate {(action ? "action" : "script")} file {file}: {exception.Message}",
                    exception);
            }
        }
    }
}
