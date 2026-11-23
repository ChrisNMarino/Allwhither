using System.Text.Json.Nodes;

namespace Omni.Windows;

internal static class ConfigurationLoader
{
    internal static JsonObject Load(string path, HashSet<string>? includeStack = null)
    {
        string fullPath = EmbeddedResources.Normalize(path);
        includeStack ??= new HashSet<string>(StringComparer.Ordinal);
        if (!includeStack.Add(fullPath))
        {
            throw new InvalidDataException($"Configuration include cycle at: {fullPath}");
        }

        JsonNode? node = JsonNode.Parse(EmbeddedResources.ReadText(fullPath));
        if (node is not JsonObject config)
        {
            throw new InvalidDataException($"Configuration {fullPath} must be a JSON object.");
        }

        var merged = new JsonObject();
        var scripts = new JsonArray();
        if (config.TryGetPropertyValue("includes", out JsonNode? includeNode))
        {
            if (includeNode is not JsonArray includes)
            {
                throw new InvalidDataException("includes must be an array.");
            }
            foreach (JsonNode? item in includes)
            {
                if (item is not JsonValue value ||
                    !value.TryGetValue(out string? include) ||
                    string.IsNullOrWhiteSpace(include))
                {
                    throw new InvalidDataException("includes must contain non-empty paths.");
                }
                string includePath = EmbeddedResources.Resolve(fullPath, include);
                JsonObject included = Load(includePath, includeStack);
                AppendResolvedScripts(scripts, included["scripts"]);
                MergeInto(merged, included);
            }
        }
        MergeInto(merged, config);
        config.TryGetPropertyValue("scripts", out JsonNode? declaredScripts);
        AppendDeclaredScripts(scripts, declaredScripts, fullPath);
        merged["scripts"] = scripts;
        includeStack.Remove(fullPath);
        return merged;
    }

    private static void AppendResolvedScripts(JsonArray target, JsonNode? declared)
    {
        if (declared is not JsonArray array)
        {
            return;
        }
        foreach (JsonNode? item in array)
        {
            if (item is JsonValue value &&
                value.TryGetValue(out string? script) &&
                !string.IsNullOrEmpty(script))
            {
                AppendScript(target, script);
            }
        }
    }

    private static void AppendDeclaredScripts(JsonArray target, JsonNode? declared, string basePath)
    {
        if (declared == null)
        {
            return;
        }
        if (declared is not JsonArray array)
        {
            throw new InvalidDataException("scripts must be an array.");
        }
        foreach (JsonNode? item in array)
        {
            if (item is not JsonValue value ||
                !value.TryGetValue(out string? script) ||
                string.IsNullOrWhiteSpace(script))
            {
                throw new InvalidDataException("scripts must contain non-empty paths.");
            }
            string resolved = EmbeddedResources.Resolve(basePath, script);
            if (!resolved.EndsWith(".js", StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidDataException(
                    "Script files must be relative .js paths without parent segments.");
            }
            AppendScript(target, resolved);
        }
    }

    private static void AppendScript(JsonArray target, string path)
    {
        foreach (JsonNode? item in target)
        {
            if (item is JsonValue value &&
                value.TryGetValue(out string? existing) &&
                string.Equals(existing, path, StringComparison.Ordinal))
            {
                return;
            }
        }
        target.Add(path);
    }

    private static void MergeInto(JsonObject target, JsonObject source)
    {
        foreach (KeyValuePair<string, JsonNode?> property in source)
        {
            if (property.Key == "includes" || property.Key == "scripts")
            {
                continue;
            }
            if (property.Value is JsonObject sourceObject &&
                target[property.Key] is JsonObject targetObject)
            {
                MergeInto(targetObject, sourceObject);
                continue;
            }
            target[property.Key] = property.Value?.DeepClone();
        }
    }
}
