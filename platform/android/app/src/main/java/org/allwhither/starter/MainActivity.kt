package org.allwhither.starter

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import java.nio.charset.StandardCharsets
import org.json.JSONArray

class MainActivity : Activity() {
    private lateinit var status: TextView
    private var openFileCallback: ((Uri?) -> Unit)? = null
    private var saveFileCallback: ((Uri?) -> Unit)? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        try {
            val configuration = UiConfiguration(BuildConfig.ALLWHITHER_PROJECT)
            val application = configuration.load()
            val ui = application.getJSONObject("ui")
            val window = ui.getJSONObject("window")
            val layout = window.getJSONObject("layout")
            val layoutType = configuration.requiredString(layout, "type")
            val applicationName = configuration.applicationName(application)
            val title = configuration.requiredString(window, "title")
            val fullscreen = configuration.optionalBoolean(window, "fullscreen")
            val fullscreenCenter = fullscreen && layoutType == "center"
            val controls = ui.getJSONObject("controls")
            val menus = ui.getJSONArray("menus")
            val theme = configuration.theme(ui)
            val chrome = configuration.chromeColors(window)

            this.title = applicationName
            status = TextView(this).apply {
                text = "Ready"
                contentDescription = "Status"
                setPadding(0, dp(8), 0, 0)
                chrome.foreground?.let { setTextColor(it) }
            }
            if (fullscreen) {
                @Suppress("DEPRECATION")
                this.window.decorView.systemUiVisibility =
                    View.SYSTEM_UI_FLAG_FULLSCREEN or
                    View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
                    View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                    View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            }
            val root = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                chrome.background?.let { setBackgroundColor(it) }
                setPadding(
                    if (fullscreenCenter) 0 else dp(16),
                    if (fullscreenCenter) 0 else dp(12),
                    if (fullscreenCenter) 0 else dp(16),
                    if (fullscreenCenter) 0 else dp(12)
                )
            }
            if (!fullscreenCenter) {
                val menuBar = LinearLayout(this).apply {
                    orientation = LinearLayout.HORIZONTAL
                    gravity = Gravity.START
                }
                MenuRenderer(this, status, configuration).addTo(menuBar, menus, chrome)
                root.addView(menuBar, matchWrap())
                root.addView(TextView(this).apply {
                    text = title
                    textSize = 20f
                    setPadding(0, dp(8), 0, dp(8))
                    chrome.foreground?.let { setTextColor(it) }
                }, matchWrap())
            }

            val controlList = LinearLayout(this).apply {
                orientation = configuration.orientationFor(layout, resources.configuration.screenWidthDp)
                gravity = if (layoutType == "center") Gravity.CENTER else Gravity.TOP
            }
            val dom = UiDom(controls, status)
            val scripts = application.optJSONArray("scripts") ?: JSONArray()
            val actionRuntime = JavaScriptActionRuntime(this, controls, dom, scripts)
            val controlRenderer = ControlRenderer(
                this, status, controls, configuration, dom, actionRuntime,
                theme.background, theme.foreground
            )
            val children = layout.getJSONArray("children")
            for (index in 0 until children.length()) {
                controlRenderer.render(
                    children.getString(index),
                    controlList,
                    centered = layoutType == "center"
                )
            }
            dom.attach(controlList, controlRenderer)
            val controlScroll = ScrollView(this).apply {
                isFillViewport = true
                addView(controlList, ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT
                ))
            }
            root.addView(controlScroll, LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                0,
                1f
            ))
            if (!fullscreenCenter) root.addView(status, matchWrap())
            setContentView(root, ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            ))
        } catch (exception: Exception) {
            setContentView(TextView(this).apply {
                text = "Invalid UI definition: ${exception.message}"
                setPadding(dp(16), dp(16), dp(16), dp(16))
            })
        }
    }

    fun pickOpenFile(title: String?, mimeTypes: Array<String>?, callback: (Uri?) -> Unit) {
        openFileCallback = callback
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = if (mimeTypes?.size == 1) mimeTypes[0] else "*/*"
            if (!mimeTypes.isNullOrEmpty() && mimeTypes.size > 1) {
                putExtra(Intent.EXTRA_MIME_TYPES, mimeTypes)
            }
            if (!title.isNullOrEmpty()) {
                putExtra(Intent.EXTRA_TITLE, title)
            }
        }
        startActivityForResult(intent, REQUEST_CODE_OPEN_FILE)
    }

    fun pickSaveFile(suggestedName: String, mimeType: String?, callback: (Uri?) -> Unit) {
        saveFileCallback = callback
        val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = mimeType ?: "text/plain"
            putExtra(Intent.EXTRA_TITLE, suggestedName)
        }
        startActivityForResult(intent, REQUEST_CODE_SAVE_FILE)
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQUEST_CODE_OPEN_FILE) {
            val callback = openFileCallback
            openFileCallback = null
            if (resultCode == Activity.RESULT_OK) {
                callback?.invoke(data?.data)
            } else {
                callback?.invoke(null)
            }
        } else if (requestCode == REQUEST_CODE_SAVE_FILE) {
            val callback = saveFileCallback
            saveFileCallback = null
            if (resultCode == Activity.RESULT_OK) {
                callback?.invoke(data?.data)
            } else {
                callback?.invoke(null)
            }
        }
    }

    fun readUriText(uri: Uri): Pair<String, String> {
        var fileName = "document"
        contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (nameIndex != -1 && cursor.moveToFirst()) {
                cursor.getString(nameIndex)?.let { fileName = it }
            }
        }
        val content = contentResolver.openInputStream(uri)?.use { stream ->
            stream.bufferedReader(StandardCharsets.UTF_8).readText()
        } ?: ""
        return fileName to content
    }

    fun writeUriText(uri: Uri, content: String): String {
        var fileName = "document"
        contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (nameIndex != -1 && cursor.moveToFirst()) {
                cursor.getString(nameIndex)?.let { fileName = it }
            }
        }
        contentResolver.openOutputStream(uri, "wt")?.use { stream ->
            stream.bufferedWriter(StandardCharsets.UTF_8).write(content)
        }
        return fileName
    }

    private fun matchWrap() = LinearLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.WRAP_CONTENT
    )

    private fun dp(value: Int): Int =
        (value * resources.displayMetrics.density).toInt()

    companion object {
        private const val REQUEST_CODE_OPEN_FILE = 2001
        private const val REQUEST_CODE_SAVE_FILE = 2002
    }
}
