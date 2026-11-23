package org.allwhither.starter

import android.graphics.Color
import java.io.File
import kotlin.math.pow
import org.json.JSONArray
import org.json.JSONObject

internal data class ThemeColors(val background: Int?, val foreground: Int?)

internal class UiConfiguration(
    private val project: String
) {
    internal fun applicationName(application: JSONObject): String {
        val definition = application.optJSONObject("application")
            ?: throw IllegalArgumentException("application must be an object.")
        return requiredString(definition, "name")
    }

    fun load(): JSONObject {
        val application = loadConfiguration("$project.json")
        applicationName(application)
        val ui = application.getJSONObject("ui")
        val window = ui.getJSONObject("window")
        val layout = window.getJSONObject("layout")
        validateLayout(layout, allowCenter = true, name = "window.layout")
        val layoutType = layout.getString("type")
        optionalBoolean(window, "fullscreen")
        positiveInt(window, "width")
        positiveInt(window, "height")
        theme(ui)
        chromeColors(window)
        val controls = ui.getJSONObject("controls")
        val children = layout.getJSONArray("children")
        validateMenus(ui.getJSONArray("menus"))
        validateControlTree(controls, children)
        return application
    }

    private fun loadConfiguration(
        path: String,
        includeStack: MutableSet<String> = mutableSetOf()
    ): JSONObject {
        val normalized = EmbeddedResources.normalize(path)
        if (!includeStack.add(normalized)) {
            throw IllegalArgumentException("Configuration include cycle at: $normalized")
        }
        val config = JSONObject(EmbeddedResources.readText(normalized))
        val merged = JSONObject()
        val scripts = JSONArray()
        if (config.has("includes")) {
            val includes = config.getJSONArray("includes")
            for (index in 0 until includes.length()) {
                val include = requiredNonEmptyString(includes.get(index), "includes")
                val includePath = EmbeddedResources.resolve(normalized, include)
                val included = loadConfiguration(includePath, includeStack)
                collectMergedScripts(scripts, included)
                mergeInto(merged, included)
            }
        }
        mergeInto(merged, config)
        collectDeclaredScripts(scripts, config, normalized)
        merged.put("scripts", scripts)
        includeStack.remove(normalized)
        return merged
    }

    private fun collectMergedScripts(target: JSONArray, included: JSONObject) {
        val declared = included.optJSONArray("scripts") ?: return
        for (index in 0 until declared.length()) {
            appendScript(target, declared.getString(index))
        }
    }

    private fun collectDeclaredScripts(target: JSONArray, source: JSONObject, basePath: String) {
        if (!source.has("scripts")) return
        val declared = source.get("scripts") as? JSONArray
            ?: throw IllegalArgumentException("scripts must be an array.")
        for (index in 0 until declared.length()) {
            val item = declared.get(index)
            if (item !is String || item.isBlank()) {
                throw IllegalArgumentException("scripts must contain non-empty paths.")
            }
            val resolved = EmbeddedResources.resolve(basePath, item)
            if (!resolved.endsWith(".js", ignoreCase = true)) {
                throw IllegalArgumentException(
                    "Script files must be relative .js paths without parent segments.")
            }
            appendScript(target, resolved)
        }
    }

    private fun appendScript(target: JSONArray, path: String) {
        for (index in 0 until target.length()) {
            if (target.getString(index) == path) return
        }
        target.put(path)
    }

    private fun mergeInto(target: JSONObject, source: JSONObject) {
        val keys = source.keys()
        while (keys.hasNext()) {
            val key = keys.next()
            if (key == "includes" || key == "scripts") continue
            val value = source.get(key)
            val existing = if (target.has(key)) target.get(key) else null
            if (value is JSONObject && existing is JSONObject) {
                mergeInto(existing, value)
            } else {
                target.put(key, value)
            }
        }
    }

    private fun validateControlTree(controls: JSONObject, order: JSONArray) {
        val visited = mutableSetOf<String>()
        val visiting = mutableSetOf<String>()
        for (index in 0 until order.length()) {
            visitControl(requiredControlId(order.get(index)), controls, visited, visiting)
        }
        val deferred = controls.keys()
        while (deferred.hasNext()) {
            val id = deferred.next()
            if (id !in visited && controls.getJSONObject(id).optString("type") == "panel") {
                visitControl(id, controls, visited, visiting)
            }
        }
        val ids = controls.keys()
        while (ids.hasNext()) {
            val id = ids.next()
            if (id !in visited) {
                throw IllegalArgumentException(
                    "Control '$id' is not reachable from window.layout.children.")
            }
        }
    }

    private fun visitControl(
        id: String,
        controls: JSONObject,
        visited: MutableSet<String>,
        visiting: MutableSet<String>
    ) {
        if (!controls.has(id)) throw IllegalArgumentException("Unknown control id: $id")
        if (id in visiting) {
            throw IllegalArgumentException("Control hierarchy contains a cycle at: $id")
        }
        if (!visited.add(id)) {
            throw IllegalArgumentException("Control id is referenced more than once: $id")
        }
        visiting.add(id)
        val control = controls.getJSONObject(id)
        validateControl(control)
        if (control.getString("type") in setOf("scrollPanel", "panel", "group")) {
            val children = control.getJSONArray("children")
            for (index in 0 until children.length()) {
                visitControl(requiredControlId(children.get(index)), controls, visited, visiting)
            }
        }
        visiting.remove(id)
    }

    private fun validateControl(control: JSONObject) {
        backgroundColor(control)
        foregroundColor(control)
        validateActions(control)
        when (requiredString(control, "type")) {
            "label" -> requiredString(control, "text")
            "button" -> {
                requiredString(control, "text")
                requiredString(control, "clickedText")
            }
            "comboBox", "radioGroup", "list" -> {
                requiredString(control, "label")
                validateSelection(control)
            }
            "textField" -> {
                requiredString(control, "label")
                stringValue(control, "value", allowEmpty = true)
                if (control.has("placeholder")) optionalString(control, "placeholder")
            }
            "textArea" -> {
                requiredString(control, "label")
                stringValue(control, "value", allowEmpty = true)
            }
            "checkbox" -> {
                requiredString(control, "label")
                requiredBoolean(control, "checked")
            }
            "slider" -> {
                requiredString(control, "label")
                val min = requiredInt(control, "min")
                val max = requiredInt(control, "max")
                val value = requiredInt(control, "value")
                if (min >= max || value !in min..max ||
                    max.toLong() - min > Int.MAX_VALUE.toLong()) {
                    throw IllegalArgumentException("slider range or value is invalid.")
                }
            }
            "scrollPanel" -> {
                requiredString(control, "label")
                positiveInt(control, "height")
                val children = control.getJSONArray("children")
                for (index in 0 until children.length()) {
                    requiredControlId(children.get(index))
                }
            }
            "group" -> {
                if (control.has("layout")) {
                    validateLayout(control.opt("layout"), allowCenter = false, name = "group.layout")
                }
                val children = control.getJSONArray("children")
                for (index in 0 until children.length()) {
                    requiredControlId(children.get(index))
                }
            }
            "panel" -> {
                requiredString(control, "label")
                val children = control.getJSONArray("children")
                for (index in 0 until children.length()) {
                    requiredControlId(children.get(index))
                }
            }
            else -> throw IllegalArgumentException("Unsupported control type.")
        }
    }

    internal fun validateLayout(layout: Any?, allowCenter: Boolean, name: String) {
        val types = if (allowCenter) listOf("vertical", "horizontal", "center")
            else listOf("vertical", "horizontal")
        val definition = layout as? JSONObject
        val type = definition?.opt("type") as? String
        if (definition == null || type !in types) {
            throw IllegalArgumentException("$name.type must be one of: ${types.joinToString(", ")}.")
        }
        if (!definition.has("media")) return
        if (type == "center") {
            throw IllegalArgumentException("$name.media is not supported for a center layout.")
        }
        val media = definition.opt("media") as? JSONArray
            ?: throw IllegalArgumentException("$name.media must be an array.")
        for (index in 0 until media.length()) {
            val ruleName = "$name.media[$index]"
            val rule = media.opt(index) as? JSONObject
            val ruleType = rule?.opt("type") as? String
            if (rule == null || (ruleType != "vertical" && ruleType != "horizontal")) {
                throw IllegalArgumentException("$ruleName.type must be 'vertical' or 'horizontal'.")
            }
            for (key in listOf("minWidth", "maxWidth")) {
                if (rule.has(key) && ((rule.opt(key) as? Int) ?: -1) < 0) {
                    throw IllegalArgumentException("$ruleName.$key must be a non-negative integer.")
                }
            }
            if (!rule.has("minWidth") && !rule.has("maxWidth")) {
                throw IllegalArgumentException("$ruleName needs minWidth or maxWidth.")
            }
        }
    }

    internal fun resolveLayoutType(layout: JSONObject?, widthDp: Int): String {
        var type = layout?.optString("type").takeUnless { it.isNullOrEmpty() } ?: "vertical"
        val media = layout?.optJSONArray("media") ?: return type
        for (index in 0 until media.length()) {
            val rule = media.getJSONObject(index)
            if (widthDp >= rule.optInt("minWidth", 0) &&
                widthDp <= rule.optInt("maxWidth", Int.MAX_VALUE)) {
                type = rule.getString("type")
            }
        }
        return type
    }

    internal fun orientationFor(layout: JSONObject?, widthDp: Int): Int =
        if (resolveLayoutType(layout, widthDp) == "horizontal") android.widget.LinearLayout.HORIZONTAL
        else android.widget.LinearLayout.VERTICAL

    private fun validateActions(control: JSONObject) {
        if (!control.has("actions")) return
        val actions = control.getJSONObject("actions")
        val supportedEvents = when (requiredString(control, "type")) {
            "button" -> setOf("click")
            "comboBox", "checkbox", "radioGroup", "list" -> setOf("change")
            "textField", "textArea", "slider" -> setOf("input")
            else -> emptySet()
        }
        val eventTypes = actions.keys()
        while (eventTypes.hasNext()) {
            val eventType = eventTypes.next()
            if (eventType !in supportedEvents) {
                throw IllegalArgumentException(
                    "${control.getString("type")} does not support the '$eventType' action event.")
            }
            val action = actions.getJSONObject(eventType)
            val file = requiredString(action, "file")
            val segments = file.replace('\\', '/').split('/')
            if (!file.endsWith(".js", ignoreCase = true) ||
                File(file).isAbsolute || file.matches(Regex("^[A-Za-z]:.*")) ||
                ".." in segments) {
                throw IllegalArgumentException(
                    "Action files must be relative .js paths without parent segments.")
            }
            val function = requiredString(action, "function")
            if (!function.matches(Regex("[A-Za-z_$][A-Za-z0-9_$]*"))) {
                throw IllegalArgumentException("Action function must be a JavaScript identifier.")
            }
        }
    }

    private fun validateSelection(control: JSONObject) {
        val options = stringArray(control.getJSONArray("options"))
        val selected = requiredInt(control, "selectedIndex")
        if (selected !in options.indices) {
            throw IllegalArgumentException("${control.getString("type")}.selectedIndex is out of range.")
        }
    }

    private fun validateMenus(menus: JSONArray) {
        for (menuIndex in 0 until menus.length()) {
            val menu = menus.getJSONObject(menuIndex)
            requiredString(menu, "label")
            val items = menu.getJSONArray("items")
            if (items.length() == 0) throw IllegalArgumentException("Menu cannot be empty.")
            for (itemIndex in 0 until items.length()) {
                val item = items.getJSONObject(itemIndex)
                requiredString(item, "text")
                requiredString(item, "message")
            }
        }
    }

    internal fun theme(ui: JSONObject): ThemeColors {
        if (!ui.has("theme")) return ThemeColors(null, null)
        val theme = ui.optJSONObject("theme")
            ?: throw IllegalArgumentException("theme must be an object.")
        return ThemeColors(
            optionalColor(theme, "backgroundColor", "theme.backgroundColor"),
            optionalColor(theme, "foregroundColor", "theme.foregroundColor")
        )
    }

    internal fun chromeColors(window: JSONObject): ThemeColors {
        val background = optionalColor(window, "backgroundColor", "window.backgroundColor")
        val explicit = optionalColor(window, "foregroundColor", "window.foregroundColor")
        val foreground = explicit ?: background?.let { contrastForeground(it) }
        return ThemeColors(background, foreground)
    }

    internal fun backgroundColor(control: JSONObject): Int? =
        optionalColor(control, "backgroundColor", "control.backgroundColor")

    internal fun foregroundColor(control: JSONObject): Int? =
        optionalColor(control, "foregroundColor", "control.foregroundColor")

    internal fun contrastForeground(color: Int): Int {
        fun linear(channel: Int): Double {
            val value = channel / 255.0
            return if (value <= 0.04045) value / 12.92 else ((value + 0.055) / 1.055).pow(2.4)
        }
        val luminance = 0.2126 * linear(Color.red(color)) +
            0.7152 * linear(Color.green(color)) +
            0.0722 * linear(Color.blue(color))
        val black = (luminance + 0.05) / 0.05
        val white = 1.05 / (luminance + 0.05)
        return if (black >= white) Color.BLACK else Color.WHITE
    }

    private fun optionalColor(source: JSONObject, key: String, label: String): Int? {
        if (!source.has(key)) return null
        val value = source.opt(key)
        if (value !is String || !value.matches(Regex("#[0-9A-Fa-f]{6}"))) {
            throw IllegalArgumentException("$label must be a #RRGGBB color.")
        }
        return Color.parseColor(value)
    }

    internal fun stringArray(array: JSONArray): List<String> =
        (0 until array.length()).map { index ->
            (array.get(index) as? String)?.takeIf { it.isNotBlank() }
                ?: throw IllegalArgumentException("Options must be non-empty strings.")
        }.also {
            if (it.isEmpty()) throw IllegalArgumentException("At least one option is required.")
        }

    internal fun requiredString(objectValue: JSONObject, key: String): String =
        requiredNonEmptyString(objectValue.opt(key), key)

    private fun requiredNonEmptyString(value: Any?, name: String): String =
        (value as? String)?.takeIf { it.isNotBlank() }
            ?: throw IllegalArgumentException("$name must be a non-empty string.")

    internal fun requiredControlId(value: Any?): String =
        requiredNonEmptyString(value, "Control references")

    internal fun stringValue(objectValue: JSONObject, key: String, allowEmpty: Boolean): String {
        val value = objectValue.opt(key) as? String
            ?: throw IllegalArgumentException("$key must be a string.")
        if (!allowEmpty && value.isBlank()) {
            throw IllegalArgumentException("$key must not be empty.")
        }
        return value
    }

    internal fun optionalString(objectValue: JSONObject, key: String): String =
        if (objectValue.has(key) && !objectValue.isNull(key)) {
            stringValue(objectValue, key, allowEmpty = true)
        } else {
            ""
        }

    internal fun requiredInt(objectValue: JSONObject, key: String): Int =
        objectValue.opt(key) as? Int
            ?: throw IllegalArgumentException("$key must be an integer.")

    internal fun requiredBoolean(objectValue: JSONObject, key: String): Boolean =
        objectValue.opt(key) as? Boolean
            ?: throw IllegalArgumentException("$key must be a boolean.")

    internal fun optionalBoolean(objectValue: JSONObject, key: String): Boolean {
        if (!objectValue.has(key)) return false
        return objectValue.opt(key) as? Boolean
            ?: throw IllegalArgumentException("$key must be a boolean.")
    }

    internal fun positiveInt(objectValue: JSONObject, key: String): Int =
        requiredInt(objectValue, key).takeIf { it > 0 }
            ?: throw IllegalArgumentException("$key must be a positive integer.")
}
