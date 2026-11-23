using System.Text;

namespace Omni.Windows;

internal static class EmbeddedResources
{
    internal static string ReadText(string relativePath)
    {
        string normalized = Normalize(relativePath);
        if (!EmbeddedResourceData.Files.TryGetValue(normalized, out byte[]? data) ||
            data == null)
        {
            throw new FileNotFoundException(
                $"Embedded resource '{normalized}' was not found.");
        }

        byte[] key = EmbeddedResourceData.Key;
        byte[] plain = new byte[data.Length];
        for (int index = 0; index < data.Length; index++)
        {
            int mask = key[index % key.Length] ^ ((index * 13 + 7) & 0xFF);
            plain[index] = (byte)(data[index] ^ mask);
        }
        return Encoding.UTF8.GetString(plain);
    }

    internal static string Resolve(string currentResource, string relativePath)
    {
        string normalizedCurrent = Normalize(currentResource);
        int slash = normalizedCurrent.LastIndexOf('/');
        string parent = slash < 0 ? "" : normalizedCurrent.Substring(0, slash);
        string combined = parent.Length == 0
            ? relativePath
            : parent + "/" + relativePath;
        return Normalize(combined);
    }

    internal static string Normalize(string relativePath)
    {
        if (string.IsNullOrEmpty(relativePath))
        {
            throw new InvalidDataException(
                "Resource path must be a non-empty relative path.");
        }

        string slashPath = relativePath.Replace('\\', '/');
        if (slashPath[0] == '/' ||
            (slashPath.Length >= 2 && char.IsLetter(slashPath[0]) && slashPath[1] == ':'))
        {
            throw new InvalidDataException(
                $"Resource path '{relativePath}' must stay inside the project.");
        }

        var parts = new List<string>();
        foreach (string segment in slashPath.Split('/'))
        {
            if (segment.Length == 0 || segment == ".")
            {
                continue;
            }
            if (segment == "..")
            {
                if (parts.Count == 0)
                {
                    throw new InvalidDataException(
                        $"Resource path '{relativePath}' must stay inside the project.");
                }
                parts.RemoveAt(parts.Count - 1);
                continue;
            }
            parts.Add(segment);
        }
        if (parts.Count == 0)
        {
            throw new InvalidDataException(
                "Resource path must be a non-empty relative path.");
        }
        return string.Join("/", parts);
    }
}
