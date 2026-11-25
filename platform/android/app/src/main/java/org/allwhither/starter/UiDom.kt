package org.allwhither.starter

import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.TextView
import org.json.JSONObject
import org.mozilla.javascript.Scriptable

class UiDom(
    private val controls: JSONObject,
    private val status: TextView
) {
    private val elements = mutableMapOf<String, View>()
    private var mountParent: LinearLayout? = null
    private var renderer: ControlRenderer? = null

    fun register(id: String, view: View) {
        elements[id] = view
    }

    internal fun attach(parent: LinearLayout, controlRenderer: ControlRenderer) {
        mountParent = parent
        renderer = controlRenderer
    }

    fun isRegistered(id: String): Boolean = elements.containsKey(id)

    fun getText(id: String): String {
        val control = definition(id)
        if (control.getString("type") == "label" || control.getString("type") == "button") {
            return findView<TextView>(id)?.text?.toString() ?: ""
        }
        val type = control.getString("type")
        if (type == "checkbox") return findView<android.widget.CheckBox>(id)?.text?.toString() ?: ""
        return findViews<TextView>(element(id))
            .firstOrNull { it !is EditText }
            ?.text?.toString()
            ?: control.optString("label", "")
    }

    fun setText(id: String, text: String) {
        val type = definition(id).getString("type")
        if (type == "label" || type == "button") {
            val target = if (type == "button") findView<Button>(id) else findView<TextView>(id)
            (target ?: throw IllegalArgumentException("Control '$id' has no text.")).text = text
            return
        }
        val caption = findViews<TextView>(element(id))
            .firstOrNull { it !is EditText }
            ?: throw IllegalArgumentException("Control '$id' does not expose writable text.")
        caption.text = text
    }

    fun getValue(id: String): String? {
        val root = element(id)
        val control = definition(id)
        return when (control.getString("type")) {
            "textField", "textArea" -> findViews<EditText>(root).firstOrNull()?.text?.toString()
            "comboBox" -> findViews<Spinner>(root).firstOrNull()?.selectedItem?.toString()
            "list" -> findViews<ListView>(root).firstOrNull()?.let { list ->
                val position = list.checkedItemPosition
                if (position < 0) null else list.adapter?.getItem(position)?.toString()
            }
            "slider" -> findViews<SeekBar>(root).firstOrNull()?.progress?.let {
                (it + control.getInt("min")).toString()
            }
            "radioGroup" -> findViews<RadioGroup>(root).firstOrNull()?.let { group ->
                group.findViewById<RadioButton>(group.checkedRadioButtonId)?.text?.toString()
            }
            else -> null
        }
    }

    fun setValue(id: String, value: String) {
        val root = element(id)
        val control = definition(id)
        when (control.getString("type")) {
            "textField", "textArea" -> findViews<EditText>(root).firstOrNull()?.setText(value)
                ?: missingValue(id)
            "comboBox" -> {
                val spinner = findViews<Spinner>(root).firstOrNull() ?: missingValue(id)
                val index = (0 until spinner.count).firstOrNull {
                    spinner.getItemAtPosition(it).toString() == value
                } ?: throw IllegalArgumentException("Value '$value' is not an option of '$id'.")
                spinner.setSelection(index)
            }
            "list" -> {
                val list = findViews<ListView>(root).firstOrNull() ?: missingValue(id)
                val index = (0 until (list.adapter?.count ?: 0)).firstOrNull {
                    list.adapter?.getItem(it).toString() == value
                } ?: throw IllegalArgumentException("Value '$value' is not an option of '$id'.")
                list.setItemChecked(index, true)
            }
            "slider" -> {
                val slider = findViews<SeekBar>(root).firstOrNull() ?: missingValue(id)
                val parsed = value.toIntOrNull()
                    ?: throw IllegalArgumentException("Slider value must be an integer.")
                slider.progress = (parsed - control.getInt("min")).coerceIn(0, slider.max)
            }
            "radioGroup" -> {
                val group = findViews<RadioGroup>(root).firstOrNull() ?: missingValue(id)
                val radio = findViews<RadioButton>(group)
                    .firstOrNull { it.text.toString() == value }
                    ?: throw IllegalArgumentException("Value '$value' is not an option of '$id'.")
                group.check(radio.id)
            }
            else -> missingValue(id)
        }
    }

    fun setVisible(id: String, visible: Boolean) {
        element(id).visibility = if (visible) View.VISIBLE else View.GONE
    }

    fun getChecked(id: String): Boolean {
        if (definition(id).getString("type") != "checkbox") {
            throw IllegalArgumentException("Control '$id' does not expose a checked state.")
        }
        val checkBox = findViews<android.widget.CheckBox>(element(id)).firstOrNull()
            ?: throw IllegalArgumentException("Control '$id' is unavailable.")
        return checkBox.isChecked
    }

    fun setChecked(id: String, checked: Boolean) {
        if (definition(id).getString("type") != "checkbox") {
            throw IllegalArgumentException("Control '$id' does not expose a checked state.")
        }
        val checkBox = findViews<android.widget.CheckBox>(element(id)).firstOrNull()
            ?: throw IllegalArgumentException("Control '$id' is unavailable.")
        checkBox.isChecked = checked
    }

    fun setSecure(id: String, secure: Boolean) {
        val editText = findViews<EditText>(element(id)).firstOrNull()
            ?: throw IllegalArgumentException("Control '$id' does not expose a secure state.")
        editText.transformationMethod = if (secure) android.text.method.PasswordTransformationMethod.getInstance() else null
    }

    fun getSecure(id: String): Boolean {
        val editText = findViews<EditText>(element(id)).firstOrNull() ?: return false
        return editText.transformationMethod is android.text.method.PasswordTransformationMethod
    }

    fun setStatus(text: String) {
        status.text = text
    }

    fun addPanel(id: String) {
        if (definition(id).getString("type") != "panel") {
            throw IllegalArgumentException("Control '$id' is not a panel.")
        }
        if (elements.containsKey(id)) {
            throw IllegalArgumentException("Panel '$id' is already shown.")
        }
        val parent = mountParent
        val controlRenderer = renderer
            ?: throw IllegalArgumentException("The window is not ready to add a panel.")
        if (parent == null) {
            throw IllegalArgumentException("The window is not ready to add a panel.")
        }
        controlRenderer.render(id, parent)
    }

    fun removePanel(id: String) {
        if (definition(id).getString("type") != "panel") {
            throw IllegalArgumentException("Control '$id' is not a panel.")
        }
        val view = elements[id] ?: throw IllegalArgumentException("Panel '$id' is not shown.")
        unregisterTree(id)
        (view.parent as? ViewGroup)?.removeView(view)
    }

    fun setOptions(id: String, options: Any?) = applyOptions(id, options, null)

    fun setOptions(id: String, options: Any?, selectedIndex: Any?) =
        applyOptions(id, options, readIndex(selectedIndex))

    fun getSelectedIndex(id: String): Int {
        val root = element(id)
        return when (definition(id).getString("type")) {
            "list" -> findViews<ListView>(root).firstOrNull()?.checkedItemPosition ?: -1
            "comboBox" -> findViews<Spinner>(root).firstOrNull()?.selectedItemPosition ?: -1
            else -> throw IllegalArgumentException(
                "Control '$id' does not expose a selection index."
            )
        }
    }

    private fun applyOptions(id: String, options: Any?, selectedIndex: Int?) {
        val type = definition(id).getString("type")
        if (type != "list" && type != "comboBox") {
            throw IllegalArgumentException("Control '$id' does not expose options.")
        }
        val values = readOptionList(options)
        val index = when {
            values.isEmpty() -> {
                if (selectedIndex != null && selectedIndex != -1) {
                    throw IllegalArgumentException("selectedIndex is out of range.")
                }
                -1
            }
            selectedIndex == null -> 0
            selectedIndex in values.indices -> selectedIndex
            else -> throw IllegalArgumentException("selectedIndex is out of range.")
        }
        val root = element(id)
        if (type == "list") {
            val list = findViews<ListView>(root).firstOrNull()
                ?: throw IllegalArgumentException("Control '$id' does not expose options.")
            list.clearChoices()
            val palette = list.tag as? OptionPalette
            list.adapter = if (palette == null) {
                ArrayAdapter(
                    list.context,
                    android.R.layout.simple_list_item_activated_1,
                    values.toMutableList()
                )
            } else {
                ColoredOptionAdapter(
                    list.context,
                    android.R.layout.simple_list_item_activated_1,
                    values,
                    palette
                )
            }
            if (index >= 0) list.setItemChecked(index, true)
            return
        }
        val spinner = findViews<Spinner>(root).firstOrNull()
            ?: throw IllegalArgumentException("Control '$id' does not expose options.")
        val palette = spinner.tag as? OptionPalette
        spinner.adapter = if (palette == null) {
            ArrayAdapter(
                spinner.context,
                android.R.layout.simple_spinner_item,
                values
            )
        } else {
            ColoredOptionAdapter(
                spinner.context,
                android.R.layout.simple_spinner_item,
                values,
                palette
            )
        }.also { it.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item) }
        if (index >= 0) spinner.setSelection(index)
    }

    private fun readOptionList(options: Any?): List<String> {
        val scriptable = options as? Scriptable
            ?: throw IllegalArgumentException("options must be an array of strings.")
        val rawLength = scriptable.get("length", scriptable)
        if (rawLength == Scriptable.NOT_FOUND) {
            throw IllegalArgumentException("options must be an array of strings.")
        }
        val length = when (rawLength) {
            is Number -> rawLength.toInt()
            else -> org.mozilla.javascript.Context.toNumber(rawLength).toInt()
        }
        if (length < 0) {
            throw IllegalArgumentException("options must be an array of strings.")
        }
        return (0 until length).map { index ->
            val item = scriptable.get(index, scriptable)
            if (item == Scriptable.NOT_FOUND) {
                throw IllegalArgumentException("options must be non-empty strings.")
            }
            val text = org.mozilla.javascript.Context.toString(item)
            if (text.isBlank()) {
                throw IllegalArgumentException("options must be non-empty strings.")
            }
            text
        }
    }

    private fun readIndex(value: Any?): Int {
        val number = when (value) {
            is Number -> value.toDouble()
            else -> org.mozilla.javascript.Context.toNumber(value)
        }
        if (number.isNaN() || number % 1.0 != 0.0) {
            throw IllegalArgumentException("selectedIndex is out of range.")
        }
        return number.toInt()
    }

    private fun unregisterTree(id: String) {
        val control = controls.optJSONObject(id)
        if (control != null) {
            val type = control.optString("type")
            if (type == "panel" || type == "scrollPanel" || type == "group") {
                val children = control.optJSONArray("children")
                if (children != null) {
                    for (index in 0 until children.length()) {
                        unregisterTree(children.getString(index))
                    }
                }
            }
        }
        elements.remove(id)
    }

    private fun definition(id: String): JSONObject =
        controls.optJSONObject(id) ?: throw IllegalArgumentException("Unknown control id: $id")

    private fun element(id: String): View =
        elements[id] ?: throw IllegalArgumentException("Control '$id' has not been rendered.")

    private inline fun <reified T : View> findView(id: String): T? =
        findViews<T>(element(id)).firstOrNull()

    private inline fun <reified T : View> findViews(root: View): List<T> {
        val found = mutableListOf<T>()
        val pending = java.util.ArrayDeque<View>()
        pending.push(root)
        while (pending.isNotEmpty()) {
            val view = pending.pop()
            if (view is T) found += view
            if (view is ViewGroup) {
                for (index in view.childCount - 1 downTo 0) {
                    pending.push(view.getChildAt(index))
                }
            }
        }
        return found
    }

    private fun missingValue(id: String): Nothing =
        throw IllegalArgumentException("Control '$id' does not expose a writable value.")
}
