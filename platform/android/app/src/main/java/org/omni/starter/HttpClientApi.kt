package org.omni.starter

import android.os.Handler
import android.os.Looper
import android.util.Log
import org.mozilla.javascript.Context
import org.mozilla.javascript.Function
import org.mozilla.javascript.NativeArray
import org.mozilla.javascript.Scriptable
import org.mozilla.javascript.ScriptableObject
import org.mozilla.javascript.Undefined
import java.io.IOException
import java.net.HttpURLConnection
import java.net.ProtocolException
import java.net.SocketTimeoutException
import java.net.URI
import java.net.URISyntaxException
import java.net.URL
import java.nio.charset.StandardCharsets
import java.util.Locale
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

class HttpClientApi {
    private val mainHandler = Handler(Looper.getMainLooper())
    private lateinit var scope: Scriptable

    fun attach(scope: Scriptable) {
        this.scope = scope
    }

    fun get(url: Any?): Any = send("GET", url, null)

    fun get(url: Any?, options: Any?): Any = send("GET", url, options)

    fun post(url: Any?): Any = send("POST", url, null)

    fun post(url: Any?, options: Any?): Any = send("POST", url, options)

    fun put(url: Any?): Any = send("PUT", url, null)

    fun put(url: Any?, options: Any?): Any = send("PUT", url, options)

    fun patch(url: Any?): Any = send("PATCH", url, null)

    fun patch(url: Any?, options: Any?): Any = send("PATCH", url, options)

    fun delete(url: Any?): Any = send("DELETE", url, null)

    fun delete(url: Any?, options: Any?): Any = send("DELETE", url, options)

    fun head(url: Any?): Any = send("HEAD", url, null)

    fun head(url: Any?, options: Any?): Any = send("HEAD", url, options)

    fun options(url: Any?): Any = send("OPTIONS", url, null)

    fun options(url: Any?, options: Any?): Any = send("OPTIONS", url, options)

    fun request(method: Any?, url: Any?): Any = request(method, url, null)

    fun request(method: Any?, url: Any?, options: Any?): Any {
        val verb = asString(method)
        if (verb == null || asString(url) == null) {
            return rejected("A method and URL are required.")
        }
        return send(verb.uppercase(Locale.US), url, options)
    }

    private fun send(method: String, urlValue: Any?, optionsValue: Any?): Any {
        return try {
            val url = asString(urlValue)
            if (url.isNullOrEmpty()) {
                return rejected("A URL is required.")
            }
            if (!isMethod(method)) {
                return rejected("Invalid HTTP method.")
            }
            val parsed = parseHttpUrl(url)
            val options = readOptions(optionsValue)
            val pending = HttpThenable()
            pending.start(method, parsed, options.timeoutMs, options.headers, options.body)
            pending
        } catch (exception: IllegalArgumentException) {
            rejected(exception.message ?: "Request failed.")
        }
    }

    private fun execute(method: String, url: URL, options: RequestOptions): HttpResult {
        val connection = url.openConnection() as HttpURLConnection
        try {
            setMethod(connection, method)
            connection.instanceFollowRedirects = true
            connection.connectTimeout = options.timeoutMs
            connection.readTimeout = options.timeoutMs
            connection.useCaches = false
            for ((name, value) in options.headers) {
                connection.setRequestProperty(name, value)
            }
            val body = options.body
            if (body != null && method != "GET" && method != "HEAD") {
                if (!hasHeader(options.headers, "Content-Type")) {
                    connection.setRequestProperty("Content-Type", "text/plain;charset=UTF-8")
                }
                val bytes = body.toByteArray(StandardCharsets.UTF_8)
                connection.doOutput = true
                connection.setFixedLengthStreamingMode(bytes.size)
                connection.outputStream.use { stream -> stream.write(bytes) }
            }
            val status = connection.responseCode
            val statusText = connection.responseMessage ?: ""
            val headers = readResponseHeaders(connection)
            val responseBody = readBody(connection, status)
            val finalUrl = connection.url?.toString() ?: url.toString()
            return HttpResult(status in 200..299, status, statusText, headers, responseBody, finalUrl)
        } finally {
            connection.disconnect()
        }
    }

    private fun readResponseHeaders(connection: HttpURLConnection): Map<String, String> {
        val headers = linkedMapOf<String, String>()
        for ((name, values) in connection.headerFields) {
            if (name == null) continue
            val key = name.lowercase(Locale.US)
            val joined = values.joinToString(", ")
            val existing = headers[key]
            headers[key] = if (existing == null) joined else "$existing, $joined"
        }
        return headers
    }

    private fun readBody(connection: HttpURLConnection, status: Int): String {
        val stream = (if (status >= 400) connection.errorStream else connection.inputStream)
            ?: return ""
        return stream.use { input ->
            String(input.readBytes(), StandardCharsets.UTF_8)
        }
    }

    private fun setMethod(connection: HttpURLConnection, method: String) {
        try {
            connection.requestMethod = method
            return
        } catch (exception: ProtocolException) {
            var type: Class<*>? = connection.javaClass
            var assigned = false
            while (type != null) {
                try {
                    val field = type.getDeclaredField("method")
                    if (field.type == String::class.java) {
                        field.isAccessible = true
                        field.set(connection, method)
                        assigned = true
                    }
                } catch (_: NoSuchFieldException) {
                }
                type = type.superclass
            }
            if (!assigned) throw exception
        }
    }

    private fun readOptions(value: Any?): RequestOptions {
        if (value == null || value is Undefined) {
            return RequestOptions()
        }
        if (value !is Scriptable || value is NativeArray || value is Function) {
            throw IllegalArgumentException("HTTP options must be an object.")
        }
        val result = RequestOptions()
        val timeout = property(value, "timeout")
        if (timeout != null) {
            val milliseconds = (timeout as? Number)?.toDouble() ?: Double.NaN
            if (!milliseconds.isFinite() || milliseconds <= 0.0 || milliseconds > Int.MAX_VALUE) {
                throw IllegalArgumentException(
                    "HTTP timeout must be a positive number of milliseconds."
                )
            }
            result.timeoutMs = milliseconds.toInt()
        }
        val headers = property(value, "headers")
        if (headers != null) {
            readHeaders(headers, result.headers)
        }
        val body = property(value, "body")
        if (body != null) {
            result.body = asString(body)
                ?: throw IllegalArgumentException("HTTP body must be a string.")
        }
        return result
    }

    private fun readHeaders(value: Any, target: MutableMap<String, String>) {
        if (value !is Scriptable || value is NativeArray || value is Function) {
            throw IllegalArgumentException("HTTP headers must be an object.")
        }
        for (id in value.ids) {
            val name = id as? String ?: continue
            if (name.isEmpty()) continue
            val raw = value.get(name, value)
            if (raw == Scriptable.NOT_FOUND || raw == null || raw is Undefined) {
                target[name] = ""
                continue
            }
            if (raw is Scriptable) {
                throw IllegalArgumentException("HTTP headers must be an object.")
            }
            target[name] = raw.toString()
        }
    }

    private fun property(source: Scriptable, name: String): Any? {
        val value = source.get(name, source)
        if (value == Scriptable.NOT_FOUND || value == null || value is Undefined) {
            return null
        }
        return value
    }

    private fun parseHttpUrl(url: String): URL {
        val uri = try {
            URI(url)
        } catch (_: URISyntaxException) {
            throw IllegalArgumentException("Invalid URL.")
        }
        val scheme = uri.scheme?.lowercase(Locale.US)
            ?: throw IllegalArgumentException("Invalid URL.")
        if (scheme != "http" && scheme != "https") {
            throw IllegalArgumentException("Only http and https URLs are supported.")
        }
        return try {
            uri.toURL()
        } catch (_: IllegalArgumentException) {
            throw IllegalArgumentException("Invalid URL.")
        } catch (_: IOException) {
            throw IllegalArgumentException("Invalid URL.")
        }
    }

    private fun rejected(message: String): HttpThenable {
        val pending = HttpThenable()
        pending.reject(message)
        return pending
    }

    private fun errorText(exception: Exception): String {
        if (exception is SocketTimeoutException) {
            return "Request timed out."
        }
        val message = exception.message
        return if (message.isNullOrEmpty()) "Request failed." else message
    }

    private fun asString(value: Any?): String? = when (value) {
        is String -> value
        is CharSequence -> value.toString()
        else -> null
    }

    private fun isMethod(method: String): Boolean {
        if (method.isEmpty() || !isAsciiLetter(method[0])) {
            return false
        }
        for (index in 1 until method.length) {
            if (!isAsciiLetterOrDigit(method[index])) {
                return false
            }
        }
        return true
    }

    private fun isAsciiLetter(char: Char): Boolean = char in 'A'..'Z' || char in 'a'..'z'

    private fun isAsciiLetterOrDigit(char: Char): Boolean =
        isAsciiLetter(char) || char in '0'..'9'

    private fun hasHeader(headers: Map<String, String>, name: String): Boolean {
        for (key in headers.keys) {
            if (key.equals(name, ignoreCase = true)) {
                return true
            }
        }
        return false
    }

    private class RequestOptions {
        var timeoutMs: Int = DEFAULT_TIMEOUT_MS
        val headers: LinkedHashMap<String, String> = linkedMapOf()
        var body: String? = null
    }

    private class HttpResult(
        val ok: Boolean,
        val status: Int,
        val statusText: String,
        val headers: Map<String, String>,
        val body: String,
        val url: String
    )

    inner class HttpThenable {
        private val lock = Any()
        private var settled = false
        private var success: HttpResult? = null
        private var failure: String? = null
        private val waiters = mutableListOf<Pair<Function?, Function?>>()

        fun start(
            method: String,
            url: URL,
            timeoutMs: Int,
            headers: Map<String, String>,
            body: String?
        ) {
            val options = RequestOptions()
            options.timeoutMs = timeoutMs
            options.headers.putAll(headers)
            options.body = body
            executor.execute {
                try {
                    settle(execute(method, url, options), null)
                } catch (exception: Exception) {
                    settle(null, errorText(exception))
                }
            }
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

        private fun settle(result: HttpResult?, message: String?) {
            val pending = synchronized(lock) {
                if (settled) {
                    return
                }
                settled = true
                success = result
                failure = message
                val copy = waiters.toList()
                waiters.clear()
                copy
            }
            if (pending.isEmpty()) {
                return
            }
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
                val result = success ?: return
                onFulfilled?.call(context, scope, scope, arrayOf(toResponse(context, result)))
            } catch (exception: Exception) {
                Log.w("omni", "JavaScript HTTP callback failed", exception)
            } finally {
                Context.exit()
            }
        }

        private fun toResponse(context: Context, result: HttpResult): Scriptable {
            val response = context.newObject(scope)
            ScriptableObject.putProperty(response, "ok", result.ok)
            ScriptableObject.putProperty(response, "status", result.status)
            ScriptableObject.putProperty(response, "statusText", result.statusText)
            ScriptableObject.putProperty(response, "body", result.body)
            ScriptableObject.putProperty(response, "url", result.url)
            val headers = context.newObject(scope)
            for ((name, value) in result.headers) {
                ScriptableObject.putProperty(headers, name, value)
            }
            ScriptableObject.putProperty(response, "headers", headers)
            return response
        }
    }

    companion object {
        private const val DEFAULT_TIMEOUT_MS = 30000

        private val executor: ExecutorService = Executors.newCachedThreadPool { runnable ->
            Thread(runnable, "omni-http").apply { isDaemon = true }
        }
    }
}
