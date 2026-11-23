const OPFS_SUPPORTED = typeof navigator !== "undefined" &&
  Boolean(navigator.storage && typeof navigator.storage.getDirectory === "function");

function reject(message) {
  return Promise.reject(message);
}

function normalizeRelativePath(path) {
  if (typeof path !== "string" || path.trim() === "") {
    throw new Error("A valid file path is required.");
  }
  const clean = path.replace(/\\/g, "/").replace(/^\/+/, "");
  const parts = clean.split("/").filter(Boolean);
  for (const part of parts) {
    if (part === "..") {
      throw new Error("Parent path traversal ('..') is not allowed.");
    }
  }
  return parts.join("/");
}

class WebFileStorage {
  constructor() {
    this.prefix = "allwhither_fs:";
  }

  async read(path) {
    const rel = normalizeRelativePath(path);
    if (OPFS_SUPPORTED) {
      try {
        const root = await navigator.storage.getDirectory();
        const parts = rel.split("/");
        const fileName = parts.pop();
        let dir = root;
        for (const part of parts) {
          dir = await dir.getDirectoryHandle(part, { create: false });
        }
        const fileHandle = await dir.getFileHandle(fileName, { create: false });
        const file = await fileHandle.getFile();
        return await file.text();
      } catch (error) {
        if (error.name === "NotFoundError") {
          throw new Error(`File not found: ${path}`);
        }
        throw new Error(`Storage read failed: ${error.message}`);
      }
    }

    // LocalStorage fallback
    const value = localStorage.getItem(this.prefix + rel);
    if (value === null) {
      throw new Error(`File not found: ${path}`);
    }
    return value;
  }

  async write(path, content) {
    const rel = normalizeRelativePath(path);
    const data = typeof content === "string" ? content : String(content ?? "");
    if (OPFS_SUPPORTED) {
      try {
        const root = await navigator.storage.getDirectory();
        const parts = rel.split("/");
        const fileName = parts.pop();
        let dir = root;
        for (const part of parts) {
          dir = await dir.getDirectoryHandle(part, { create: true });
        }
        const fileHandle = await dir.getFileHandle(fileName, { create: true });
        const writable = await fileHandle.createWritable();
        await writable.write(data);
        await writable.close();
        return { ok: true, path: rel };
      } catch (error) {
        throw new Error(`Storage write failed: ${error.message}`);
      }
    }

    localStorage.setItem(this.prefix + rel, data);
    return { ok: true, path: rel };
  }

  async delete(path) {
    const rel = normalizeRelativePath(path);
    if (OPFS_SUPPORTED) {
      try {
        const root = await navigator.storage.getDirectory();
        const parts = rel.split("/");
        const target = parts.pop();
        let dir = root;
        for (const part of parts) {
          dir = await dir.getDirectoryHandle(part, { create: false });
        }
        await dir.removeEntry(target, { recursive: true });
        return { ok: true };
      } catch (error) {
        if (error.name === "NotFoundError") {
          return { ok: true };
        }
        throw new Error(`Storage delete failed: ${error.message}`);
      }
    }

    localStorage.removeItem(this.prefix + rel);
    return { ok: true };
  }

  async exists(path) {
    const rel = normalizeRelativePath(path);
    if (OPFS_SUPPORTED) {
      try {
        const root = await navigator.storage.getDirectory();
        const parts = rel.split("/");
        const target = parts.pop();
        let dir = root;
        for (const part of parts) {
          dir = await dir.getDirectoryHandle(part, { create: false });
        }
        try {
          await dir.getFileHandle(target, { create: false });
          return true;
        } catch {
          await dir.getDirectoryHandle(target, { create: false });
          return true;
        }
      } catch {
        return false;
      }
    }

    return localStorage.getItem(this.prefix + rel) !== null;
  }

  async list(path = "") {
    const rel = path ? normalizeRelativePath(path) : "";
    if (OPFS_SUPPORTED) {
      try {
        const root = await navigator.storage.getDirectory();
        let dir = root;
        if (rel !== "") {
          for (const part of rel.split("/")) {
            dir = await dir.getDirectoryHandle(part, { create: false });
          }
        }
        const entries = [];
        for await (const [name] of dir.entries()) {
          entries.push(name);
        }
        return entries;
      } catch (error) {
        if (error.name === "NotFoundError") {
          return [];
        }
        throw new Error(`Storage list failed: ${error.message}`);
      }
    }

    const prefix = this.prefix + (rel ? `${rel}/` : "");
    const entries = new Set();
    for (let i = 0; i < localStorage.length; i++) {
      const key = localStorage.key(i);
      if (key && key.startsWith(prefix)) {
        const sub = key.slice(prefix.length).split("/")[0];
        if (sub) entries.add(sub);
      }
    }
    return Array.from(entries);
  }
}

export class FileApi {
  constructor() {
    this.storage = new WebFileStorage();
    this.handleStore = new Map();
    this.nextHandleId = 1;

    // Direct convenience shortcuts
    this.read = (path) => this.storage.read(path);
    this.write = (path, content) => this.storage.write(path, content);
    this.delete = (path) => this.storage.delete(path);
    this.exists = (path) => this.storage.exists(path);
    this.list = (path) => this.storage.list(path);
  }

  async open(options = {}) {
    if (typeof window !== "undefined" && typeof window.showOpenFilePicker === "function") {
      try {
        const pickerOptions = {};
        if (Array.isArray(options.filters) && options.filters.length > 0) {
          pickerOptions.types = options.filters.map((filter) => {
            const exts = (filter.extensions || []).map((ext) =>
              ext.startsWith(".") ? ext : `.${ext}`
            );
            return {
              description: filter.name || "Supported files",
              accept: { "*/*": exts },
            };
          });
        }
        const [handle] = await window.showOpenFilePicker(pickerOptions);
        if (!handle) {
          return { ok: false, canceled: true };
        }
        const file = await handle.getFile();
        const content = await file.text();
        const handleId = `web-fh-${this.nextHandleId++}`;
        this.handleStore.set(handleId, handle);
        return {
          ok: true,
          canceled: false,
          name: file.name,
          content,
          path: null,
          handle: handleId,
        };
      } catch (error) {
        if (error.name === "AbortError") {
          return { ok: false, canceled: true };
        }
        // Fall back to <input type="file"> on SecurityError / user interaction restrictions
      }
    }

    // Input fallback for unsupported browsers (Firefox, Safari, mobile)
    return new Promise((resolve) => {
      const input = document.createElement("input");
      input.type = "file";
      input.style.display = "none";
      if (Array.isArray(options.filters) && options.filters.length > 0) {
        const acceptList = options.filters.flatMap((f) =>
          (f.extensions || []).map((ext) => (ext.startsWith(".") ? ext : `.${ext}`))
        );
        if (acceptList.length > 0) {
          input.accept = acceptList.join(",");
        }
      }

      input.addEventListener("change", async () => {
        const file = input.files?.[0];
        input.remove();
        if (!file) {
          resolve({ ok: false, canceled: true });
          return;
        }
        try {
          const content = await file.text();
          resolve({
            ok: true,
            canceled: false,
            name: file.name,
            content,
            path: null,
            handle: null,
          });
        } catch {
          resolve({ ok: false, canceled: false, error: "Failed to read file." });
        }
      });

      window.addEventListener(
        "focus",
        () => {
          setTimeout(() => {
            if (!input.files || input.files.length === 0) {
              input.remove();
              resolve({ ok: false, canceled: true });
            }
          }, 600);
        },
        { once: true }
      );

      document.body.appendChild(input);
      input.click();
    });
  }

  async save(options = {}) {
    const content = typeof options.content === "string" ? options.content : String(options.content ?? "");
    const handleId = options.handle;

    // If existing handle provided from a previous open/save, write in-place
    if (handleId && this.handleStore.has(handleId)) {
      try {
        const handle = this.handleStore.get(handleId);
        const writable = await handle.createWritable();
        await writable.write(content);
        await writable.close();
        return {
          ok: true,
          canceled: false,
          name: handle.name,
          path: null,
          handle: handleId,
        };
      } catch (error) {
        if (error.name === "AbortError") {
          return { ok: false, canceled: true };
        }
        // Fall through to showSaveFilePicker
      }
    }

    return this.saveAs(options);
  }

  async saveAs(options = {}) {
    const content = typeof options.content === "string" ? options.content : String(options.content ?? "");
    const suggestedName = options.name || "untitled.txt";

    if (typeof window !== "undefined" && typeof window.showSaveFilePicker === "function") {
      try {
        const pickerOptions = { suggestedName };
        if (Array.isArray(options.filters) && options.filters.length > 0) {
          pickerOptions.types = options.filters.map((filter) => {
            const exts = (filter.extensions || []).map((ext) =>
              ext.startsWith(".") ? ext : `.${ext}`
            );
            return {
              description: filter.name || "Supported files",
              accept: { "*/*": exts },
            };
          });
        }
        const handle = await window.showSaveFilePicker(pickerOptions);
        const writable = await handle.createWritable();
        await writable.write(content);
        await writable.close();
        const handleId = `web-fh-${this.nextHandleId++}`;
        this.handleStore.set(handleId, handle);
        return {
          ok: true,
          canceled: false,
          name: handle.name,
          path: null,
          handle: handleId,
        };
      } catch (error) {
        if (error.name === "AbortError") {
          return { ok: false, canceled: true };
        }
        // Fall back to blob download
      }
    }

    // Download fallback for browsers without File System Access API
    const blob = new Blob([content], { type: "text/plain;charset=utf-8" });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = suggestedName;
    link.style.display = "none";
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    return {
      ok: true,
      canceled: false,
      name: suggestedName,
      path: null,
      handle: null,
    };
  }
}
