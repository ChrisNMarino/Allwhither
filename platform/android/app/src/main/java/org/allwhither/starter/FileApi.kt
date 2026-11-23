package org.allwhither.starter

import android.os.Handler
import android.os.Looper
import android.util.Log
import org.mozilla.javascript.Context
import org.mozilla.javascript.Function
import org.mozilla.javascript.Scriptable
import org.mozilla.javascript.ScriptableObject
import java.io.File
import java.nio.charset.StandardCharsets
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

data class FileOpenResult(
    val ok: Boolean,
    val canceled: Boolean,
    val name: String?,
    val content: String?,
    val path: String?
)

data class FileSaveResult(
    val ok: Boolean,
    val canceled: Boolean,
    val name: String?,
    val path: String?
)

data class FileStorageWriteResult(
    val ok: Boolean,
    val path: String
)

data class FileStorageDeleteResult(
    val ok: Boolean
)

class FileApi(private val activity: MainActivity) {
    private val mainHandler = Handler(Looper.getMainLooper())
    private lateinit var scope: Scriptable
    val storage = FileStorageApi(activity)

    fun attach(scope: Scriptable) {
        this.scope = scope
        storage.attach(scope)
    }

    fun open(): Any = open(null)

    fun open(options: Any?): Any {
        val pending = FileThenable()
        var title: String? = null
        var mimeTypes: Array<String>? = null

        if (options is Scriptable) {
            val titleVal = options.get("title", options)
            if (titleVal is String) title = titleVal

            val filtersVal = options.get("filters", options)
            if (filtersVal is Scriptable) {
                val mimes = mutableListOf<String>()
                val length = (filtersVal.get("length", filtersVal) as? Number)?.toInt() ?: 0
                for (i in 0 until length) {
                    val item = filtersVal.get(i, filtersVal) as? Scriptable ?: continue
                    val exts = item.get("extensions", item) as? Scriptable ?: continue
                    val extLen = (exts.get("length", exts) as? Number)?.toInt() ?: 0
                    for (j in 0 until extLen) {
                        val ext = (exts.get(j, exts) as? String)?.lowercase()?.removePrefix(".")
                        if (ext != null) {
                            when (ext) {
                                "txt" -> mimes.add("text/plain")
                                "json" -> mimes.add("application/json")
                                "html", "htm" -> mimes.add("text/html")
                                "xml" -> mimes.add("application/xml")
                                "png" -> mimes.add("image/png")
                                "jpg", "jpeg" -> mimes.add("image/jpeg")
                                else -> mimes.add("*/*")
                            }
                        }
                    }
                }
                if (mimes.isNotEmpty()) mimeTypes = mimes.distinct().toTypedArray()
            }
        }

        activity.pickOpenFile(title, mimeTypes) { uri ->
            if (uri == null) {
                pending.resolve(FileOpenResult(ok = false, canceled = true, name = null, content = null, path = null))
            } else {
                executor.execute {
                    try {
                        val (fileName, content) = activity.readUriText(uri)
                        pending.resolve(
                            FileOpenResult(
                                ok = true,
                                canceled = false,
                                name = fileName,
                                content = content,
                                path = uri.toString()
                            )
                        )
                    } catch (e: Exception) {
                        pending.reject("Failed to read file: ${e.message}")
                    }
                }
            }
        }
        return pending
    }

    fun save(): Any = save(null)

    fun save(options: Any?): Any = saveInternal(options, forceSaveAs = false)

    fun saveAs(): Any = saveAs(null)

    fun saveAs(options: Any?): Any = saveInternal(options, forceSaveAs = true)

    private fun saveInternal(options: Any?, forceSaveAs: Boolean): Any {
        val pending = FileThenable()
        var content = ""
        var targetPath: String? = null
        var suggestedName = "untitled.txt"

        if (options is Scriptable) {
            val contentVal = options.get("content", options)
            if (contentVal is String) content = contentVal

            val pathVal = options.get("path", options)
            if (pathVal is String && pathVal.isNotEmpty()) targetPath = pathVal

            val nameVal = options.get("name", options)
            if (nameVal is String && nameVal.isNotEmpty()) suggestedName = nameVal
        }

        if (!forceSaveAs && targetPath != null && targetPath.startsWith("content://")) {
            executor.execute {
                try {
                    val uri = android.net.Uri.parse(targetPath)
                    val name = activity.writeUriText(uri, content)
                    pending.resolve(FileSaveResult(ok = true, canceled = false, name = name, path = targetPath))
                } catch (e: Exception) {
                    pending.reject("Failed to write file: ${e.message}")
                }
            }
            return pending
        }

        activity.pickSaveFile(suggestedName, "text/plain") { uri ->
            if (uri == null) {
                pending.resolve(FileSaveResult(ok = false, canceled = true, name = null, path = null))
            } else {
                executor.execute {
                    try {
                        val name = activity.writeUriText(uri, content)
                        pending.resolve(FileSaveResult(ok = true, canceled = false, name = name, path = uri.toString()))
                    } catch (e: Exception) {
                        pending.reject("Failed to write file: ${e.message}")
                    }
                }
            }
        }
        return pending
    }

    fun read(path: Any?): Any = storage.read(path)
    fun write(path: Any?, content: Any?): Any = storage.write(path, content)
    fun delete(path: Any?): Any = storage.delete(path)
    fun exists(path: Any?): Any = storage.exists(path)
    fun list(): Any = storage.list(null)
    fun list(path: Any?): Any = storage.list(path)

    inner class FileStorageApi(private val activity: MainActivity) {
        private lateinit var scope: Scriptable
        private val storageRoot: File
            get() = File(activity.filesDir, "storage").apply { if (!exists()) mkdirs() }

        fun attach(scope: Scriptable) {
            this.scope = scope
        }

        private fun resolvePath(rel: String): File {
            if (rel.isBlank()) throw IllegalArgumentException("A valid file path is required.")
            val clean = rel.replace('\\', '/').trimStart('/')
            if (clean.contains("..")) throw SecurityException("Path traversal is not allowed.")
            val target = File(storageRoot, clean).canonicalFile
            if (!target.path.startsWith(storageRoot.canonicalPath)) {
                throw SecurityException("Path traversal is not allowed.")
            }
            return target
        }

        fun read(path: Any?): Any {
            val pending = FileThenable()
            executor.execute {
                try {
                    val pathStr = path as? String ?: throw IllegalArgumentException("A file path string is required.")
                    val file = resolvePath(pathStr)
                    if (!file.exists() || !file.isFile) {
                        pending.reject("File not found: $pathStr")
                    } else {
                        val content = file.readText(StandardCharsets.UTF_8)
                        pending.resolve(content)
                    }
                } catch (e: Exception) {
                    pending.reject(e.message ?: "Storage read failed.")
                }
            }
            return pending
        }

        fun write(path: Any?, content: Any?): Any {
            val pending = FileThenable()
            executor.execute {
                try {
                    val pathStr = path as? String ?: throw IllegalArgumentException("A file path string is required.")
                    val data = when (content) {
                        is String -> content
                        null -> ""
                        else -> content.toString()
                    }
                    val file = resolvePath(pathStr)
                    file.parentFile?.mkdirs()
                    file.writeText(data, StandardCharsets.UTF_8)
                    pending.resolve(FileStorageWriteResult(ok = true, path = pathStr))
                } catch (e: Exception) {
                    pending.reject(e.message ?: "Storage write failed.")
                }
            }
            return pending
        }

        fun delete(path: Any?): Any {
            val pending = FileThenable()
            executor.execute {
                try {
                    val pathStr = path as? String ?: throw IllegalArgumentException("A file path string is required.")
                    val file = resolvePath(pathStr)
                    if (file.exists()) {
                        file.deleteRecursively()
                    }
                    pending.resolve(FileStorageDeleteResult(ok = true))
                } catch (e: Exception) {
                    pending.reject(e.message ?: "Storage delete failed.")
                }
            }
            return pending
        }

        fun exists(path: Any?): Any {
            val pending = FileThenable()
            executor.execute {
                try {
                    val pathStr = path as? String ?: ""
                    val file = resolvePath(pathStr)
                    pending.resolve(file.exists())
                } catch (_: Exception) {
                    pending.resolve(false)
                }
            }
            return pending
        }

        fun list(): Any = list(null)

        fun list(path: Any?): Any {
            val pending = FileThenable()
            executor.execute {
                try {
                    val pathStr = path as? String
                    val dir = if (pathStr.isNullOrBlank()) storageRoot else resolvePath(pathStr)
                    val entries = if (dir.exists() && dir.isDirectory) {
                        dir.list()?.toList() ?: emptyList()
                    } else {
                        emptyList()
                    }
                    pending.resolve(entries)
                } catch (e: Exception) {
                    pending.reject(e.message ?: "Storage list failed.")
                }
            }
            return pending
        }
    }

    inner class FileThenable {
        private val lock = Any()
        private var settled = false
        private var success: Any? = null
        private var failure: String? = null
        private val waiters = mutableListOf<Pair<Function?, Function?>>()

        fun resolve(value: Any?) {
            settle(value, null)
        }

        fun reject(message: String) {
            settle(null, message)
        }

        fun then(onFulfilled: Any?) {
            arm(onFulfilled, null)
        }

        fun then(onFulfilled: Any?, onRejected: Any?) {
            arm(onFulfilled, onRejected)
        }

        private fun settle(result: Any?, message: String?) {
            val pending = synchronized(lock) {
                if (settled) return
                settled = true
                success = result
                failure = message
                val copy = waiters.toList()
                waiters.clear()
                copy
            }
            if (pending.isEmpty()) return
            mainHandler.post {
                for (waiter in pending) {
                    deliver(waiter.first, waiter.second)
                }
            }
        }

        private fun arm(onFulfilled: Any?, onRejected: Any?) {
            val fulfilled = onFulfilled as? Function
            val rejected = onRejected as? Function
            val deliverNow = synchronized(lock) {
                if (!settled) {
                    waiters.add(fulfilled to rejected)
                    false
                } else {
                    true
                }
            }
            if (deliverNow) {
                mainHandler.post { deliver(fulfilled, rejected) }
            }
        }

        private fun deliver(onFulfilled: Function?, onRejected: Function?) {
            val context = Context.enter()
            try {
                context.optimizationLevel = -1
                val message = failure
                if (message != null) {
                    onRejected?.call(context, scope, scope, arrayOf(message))
                    return
                }
                val result = success
                val jsArg = when (result) {
                    is FileOpenResult -> {
                        val obj = context.newObject(scope)
                        ScriptableObject.putProperty(obj, "ok", result.ok)
                        ScriptableObject.putProperty(obj, "canceled", result.canceled)
                        ScriptableObject.putProperty(obj, "name", result.name)
                        ScriptableObject.putProperty(obj, "content", result.content)
                        ScriptableObject.putProperty(obj, "path", result.path)
                        obj
                    }
                    is FileSaveResult -> {
                        val obj = context.newObject(scope)
                        ScriptableObject.putProperty(obj, "ok", result.ok)
                        ScriptableObject.putProperty(obj, "canceled", result.canceled)
                        ScriptableObject.putProperty(obj, "name", result.name)
                        ScriptableObject.putProperty(obj, "path", result.path)
                        obj
                    }
                    is FileStorageWriteResult -> {
                        val obj = context.newObject(scope)
                        ScriptableObject.putProperty(obj, "ok", result.ok)
                        ScriptableObject.putProperty(obj, "path", result.path)
                        obj
                    }
                    is FileStorageDeleteResult -> {
                        val obj = context.newObject(scope)
                        ScriptableObject.putProperty(obj, "ok", result.ok)
                        obj
                    }
                    is List<*> -> {
                        context.newArray(scope, result.map { it?.toString() ?: "" }.toTypedArray())
                    }
                    else -> Context.javaToJS(result, scope)
                }
                onFulfilled?.call(context, scope, scope, arrayOf(jsArg))
            } catch (exception: Exception) {
                Log.w("allwhither", "JavaScript File callback failed", exception)
            } finally {
                Context.exit()
            }
        }
    }

    companion object {
        private val executor: ExecutorService = Executors.newCachedThreadPool { runnable ->
            Thread(runnable, "allwhither-file").apply { isDaemon = true }
        }
    }
}
