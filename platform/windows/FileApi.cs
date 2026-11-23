using System.Text.Json;
using System.Windows.Forms;
using Jint;
using Jint.Native;
using Jint.Native.Array;
using Jint.Native.Object;
using Jint.Runtime;

namespace Allwhither.Windows;

public sealed class FileApi
{
    private readonly Engine engine;
    public FileStorageApi storage { get; }

    public FileApi(Engine engine)
    {
        this.engine = engine;
        storage = new FileStorageApi(engine);
    }

    public FileThenable open() => Open(JsValue.Undefined);
    public FileThenable open(JsValue options) => Open(options);

    public FileThenable save() => Save(JsValue.Undefined, false);
    public FileThenable save(JsValue options) => Save(options, false);

    public FileThenable saveAs() => Save(JsValue.Undefined, true);
    public FileThenable saveAs(JsValue options) => Save(options, true);

    public FileThenable read(JsValue path) => storage.read(path);
    public FileThenable write(JsValue path, JsValue content) => storage.write(path, content);
    public FileThenable delete(JsValue path) => storage.delete(path);
    public FileThenable exists(JsValue path) => storage.exists(path);
    public FileThenable list() => storage.list();
    public FileThenable list(JsValue path) => storage.list(path);

    private FileThenable Open(JsValue options)
    {
        SynchronizationContext? context = SynchronizationContext.Current;
        var tcs = new TaskCompletionSource<JsValue>();

        void ShowDialog()
        {
            try
            {
                using var dialog = new OpenFileDialog();
                ApplyOptions(dialog, options);

                if (dialog.ShowDialog() == DialogResult.OK)
                {
                    string content = File.ReadAllText(dialog.FileName);
                    var result = new JsObject(engine);
                    result.Set("ok", true);
                    result.Set("canceled", false);
                    result.Set("name", Path.GetFileName(dialog.FileName));
                    result.Set("content", content);
                    result.Set("path", dialog.FileName);
                    tcs.SetResult(result);
                }
                else
                {
                    var result = new JsObject(engine);
                    result.Set("ok", false);
                    result.Set("canceled", true);
                    tcs.SetResult(result);
                }
            }
            catch (Exception ex)
            {
                tcs.SetException(ex);
            }
        }

        if (context != null)
        {
            context.Post(_ => ShowDialog(), null);
        }
        else
        {
            ShowDialog();
        }

        return FileThenable.FromTask(engine, tcs.Task, context);
    }

    private FileThenable Save(JsValue options, bool forceSaveAs)
    {
        SynchronizationContext? context = SynchronizationContext.Current;
        var tcs = new TaskCompletionSource<JsValue>();

        string content = "";
        string? targetPath = null;
        string suggestedName = "untitled.txt";

        if (options.IsObject())
        {
            var obj = options.AsObject();
            if (obj.HasProperty("content"))
            {
                content = obj.Get("content").AsString();
            }
            if (obj.HasProperty("path") && !obj.Get("path").IsNull() && !obj.Get("path").IsUndefined())
            {
                targetPath = obj.Get("path").AsString();
            }
            if (obj.HasProperty("name") && !obj.Get("name").IsNull() && !obj.Get("name").IsUndefined())
            {
                suggestedName = obj.Get("name").AsString();
            }
        }

        void PerformSave()
        {
            try
            {
                if (!forceSaveAs && !string.IsNullOrEmpty(targetPath))
                {
                    File.WriteAllText(targetPath, content);
                    var result = new JsObject(engine);
                    result.Set("ok", true);
                    result.Set("canceled", false);
                    result.Set("name", Path.GetFileName(targetPath));
                    result.Set("path", targetPath);
                    tcs.SetResult(result);
                    return;
                }

                using var dialog = new SaveFileDialog();
                dialog.FileName = suggestedName;
                ApplyOptions(dialog, options);

                if (dialog.ShowDialog() == DialogResult.OK)
                {
                    File.WriteAllText(dialog.FileName, content);
                    var result = new JsObject(engine);
                    result.Set("ok", true);
                    result.Set("canceled", false);
                    result.Set("name", Path.GetFileName(dialog.FileName));
                    result.Set("path", dialog.FileName);
                    tcs.SetResult(result);
                }
                else
                {
                    var result = new JsObject(engine);
                    result.Set("ok", false);
                    result.Set("canceled", true);
                    tcs.SetResult(result);
                }
            }
            catch (Exception ex)
            {
                tcs.SetException(ex);
            }
        }

        if (context != null)
        {
            context.Post(_ => PerformSave(), null);
        }
        else
        {
            PerformSave();
        }

        return FileThenable.FromTask(engine, tcs.Task, context);
    }

    private static void ApplyOptions(FileDialog dialog, JsValue options)
    {
        if (!options.IsObject()) return;
        var obj = options.AsObject();

        if (obj.HasProperty("title") && obj.Get("title").IsString())
        {
            dialog.Title = obj.Get("title").AsString();
        }

        if (obj.HasProperty("filters") && obj.Get("filters").IsArray())
        {
            var filtersArray = obj.Get("filters").AsArray();
            var filterParts = new List<string>();
            for (uint i = 0; i < filtersArray.Length; i++)
            {
                var filterItem = filtersArray.Get(i);
                if (filterItem.IsObject())
                {
                    var fObj = filterItem.AsObject();
                    string name = fObj.HasProperty("name") ? fObj.Get("name").AsString() : "Files";
                    var extList = new List<string>();
                    if (fObj.HasProperty("extensions") && fObj.Get("extensions").IsArray())
                    {
                        var exts = fObj.Get("extensions").AsArray();
                        for (uint j = 0; j < exts.Length; j++)
                        {
                            string ext = exts.Get(j).AsString();
                            extList.Add(ext.StartsWith('*') ? ext : (ext.StartsWith('.') ? $"*{ext}" : $"*.{ext}"));
                        }
                    }
                    string pattern = extList.Count > 0 ? string.Join(";", extList) : "*.*";
                    filterParts.Add($"{name}|{pattern}");
                }
            }
            if (filterParts.Count > 0)
            {
                filterParts.Add("All Files (*.*)|*.*");
                dialog.Filter = string.Join("|", filterParts);
            }
        }
    }
}

public sealed class FileStorageApi
{
    private readonly Engine engine;
    private readonly string storageRoot;

    public FileStorageApi(Engine engine)
    {
        this.engine = engine;
        string appData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        storageRoot = Path.GetFullPath(Path.Combine(appData, "allwhither", EmbeddedResourceData.ProjectName, "storage"));
    }

    private string ResolvePath(string relativePath)
    {
        if (string.IsNullOrWhiteSpace(relativePath))
        {
            throw new ArgumentException("A valid file path is required.");
        }
        string clean = relativePath.Replace('/', Path.DirectorySeparatorChar).Replace('\\', Path.DirectorySeparatorChar).TrimStart(Path.DirectorySeparatorChar);
        string full = Path.GetFullPath(Path.Combine(storageRoot, clean));
        if (!full.StartsWith(storageRoot, StringComparison.OrdinalIgnoreCase))
        {
            throw new UnauthorizedAccessException("Path traversal is not allowed.");
        }
        return full;
    }

    public FileThenable read(JsValue path)
    {
        SynchronizationContext? context = SynchronizationContext.Current;
        try
        {
            string fullPath = ResolvePath(path.AsString());
            if (!File.Exists(fullPath))
            {
                return FileThenable.Failed(engine, $"File not found: {path}", context);
            }
            string content = File.ReadAllText(fullPath);
            return FileThenable.Resolved(engine, new JsString(content), context);
        }
        catch (Exception ex)
        {
            return FileThenable.Failed(engine, ex.Message, context);
        }
    }

    public FileThenable write(JsValue path, JsValue content)
    {
        SynchronizationContext? context = SynchronizationContext.Current;
        try
        {
            string fullPath = ResolvePath(path.AsString());
            string? dir = Path.GetDirectoryName(fullPath);
            if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
            {
                Directory.CreateDirectory(dir);
            }
            string data = content.IsUndefined() ? "" : (content.IsNull() ? "" : content.AsString());
            File.WriteAllText(fullPath, data);

            var result = new JsObject(engine);
            result.Set("ok", true);
            result.Set("path", path.AsString());
            return FileThenable.Resolved(engine, result, context);
        }
        catch (Exception ex)
        {
            return FileThenable.Failed(engine, ex.Message, context);
        }
    }

    public FileThenable delete(JsValue path)
    {
        SynchronizationContext? context = SynchronizationContext.Current;
        try
        {
            string fullPath = ResolvePath(path.AsString());
            if (File.Exists(fullPath))
            {
                File.Delete(fullPath);
            }
            else if (Directory.Exists(fullPath))
            {
                Directory.Delete(fullPath, true);
            }
            var result = new JsObject(engine);
            result.Set("ok", true);
            return FileThenable.Resolved(engine, result, context);
        }
        catch (Exception ex)
        {
            return FileThenable.Failed(engine, ex.Message, context);
        }
    }

    public FileThenable exists(JsValue path)
    {
        SynchronizationContext? context = SynchronizationContext.Current;
        try
        {
            string fullPath = ResolvePath(path.AsString());
            bool exist = File.Exists(fullPath) || Directory.Exists(fullPath);
            return FileThenable.Resolved(engine, (JsBoolean)exist, context);
        }
        catch
        {
            return FileThenable.Resolved(engine, (JsBoolean)false, context);
        }
    }

    public FileThenable list() => list(JsValue.Undefined);

    public FileThenable list(JsValue path)
    {
        SynchronizationContext? context = SynchronizationContext.Current;
        try
        {
            string targetDir = storageRoot;
            if (path.IsString() && !string.IsNullOrWhiteSpace(path.AsString()))
            {
                targetDir = ResolvePath(path.AsString());
            }

            var array = new JsArray(engine);
            if (Directory.Exists(targetDir))
            {
                string[] entries = Directory.GetFileSystemEntries(targetDir);
                foreach (string entry in entries)
                {
                    array.Push(new JsString(Path.GetFileName(entry)));
                }
            }
            return FileThenable.Resolved(engine, array, context);
        }
        catch (Exception ex)
        {
            return FileThenable.Failed(engine, ex.Message, context);
        }
    }
}

public sealed class FileThenable
{
    private readonly Engine engine;
    private readonly Task<JsValue> task;
    private readonly SynchronizationContext? context;

    private FileThenable(Engine engine, Task<JsValue> task, SynchronizationContext? context)
    {
        this.engine = engine;
        this.task = task;
        this.context = context;
    }

    public static FileThenable FromTask(Engine engine, Task<JsValue> task, SynchronizationContext? context) =>
        new(engine, task, context);

    public static FileThenable Resolved(Engine engine, JsValue value, SynchronizationContext? context) =>
        new(engine, Task.FromResult(value), context);

    public static FileThenable Failed(Engine engine, string message, SynchronizationContext? context) =>
        new(engine, Task.FromException<JsValue>(new InvalidOperationException(message)), context);

    public void then(JsValue onFulfilled) => Arm(onFulfilled, JsValue.Undefined);
    public void then(JsValue onFulfilled, JsValue onRejected) => Arm(onFulfilled, onRejected);

    private void Arm(JsValue onFulfilled, JsValue onRejected)
    {
        task.ContinueWith(completed =>
        {
            void Deliver()
            {
                try
                {
                    if (completed.IsFaulted || completed.IsCanceled)
                    {
                        string error = completed.Exception?.GetBaseException().Message ?? "File operation failed.";
                        Invoke(onRejected, error);
                        return;
                    }
                    Invoke(onFulfilled, completed.Result);
                }
                catch (JavaScriptException)
                {
                }
            }

            if (context != null)
            {
                context.Post(_ => Deliver(), null);
            }
            else
            {
                Deliver();
            }
        }, TaskScheduler.Default);
    }

    private void Invoke(JsValue callback, object argument)
    {
        if (callback.IsUndefined() || callback.IsNull())
        {
            return;
        }
        engine.Invoke(callback, argument);
    }
}
