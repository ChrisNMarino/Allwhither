package org.allwhither.starter

import android.app.Activity
import android.content.res.ColorStateList
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.text.InputType
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.TextView
import org.json.JSONObject

internal class ControlRenderer(
    private val activity: Activity,
    private val status: TextView,
    private val controls: JSONObject,
    private val uiConfiguration: UiConfiguration,
    private val dom: UiDom,
    private val actionRuntime: JavaScriptActionRuntime,
    private val themeBackground: Int? = null,
    private val themeForeground: Int? = null
) {
    private val controlViewMarker = Any()

    fun render(controlId: String, parent: LinearLayout, centered: Boolean = false) {
        val control = controls.getJSONObject(controlId)
        val palette = paletteFor(control)
        val controlRoot = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
        }
        val type = uiConfiguration.requiredString(control, "type")
        when (type) {
            "label" -> controlRoot.addView(TextView(activity).apply {
                text = uiConfiguration.requiredString(control, "text")
                if (centered) {
                    gravity = Gravity.CENTER_HORIZONTAL
                    textAlignment = View.TEXT_ALIGNMENT_CENTER
                }
            }, if (centered) wrapWrap() else matchWrap())
            "button" -> controlRoot.addView(Button(activity).apply {
                text = uiConfiguration.requiredString(control, "text")
                setOnClickListener {
                    text = uiConfiguration.requiredString(control, "clickedText")
                    status.text = text
                    actionRuntime.invoke(controlId, "click")
                }
            }, wrapWrap())
            "comboBox" -> {
                val label = uiConfiguration.requiredString(control, "label")
                controlRoot.addView(fieldLabel(label), matchWrap())
                val options = uiConfiguration.stringArray(control.getJSONArray("options"))
                controlRoot.addView(Spinner(activity).apply {
                    adapter = optionAdapter(
                        options,
                        android.R.layout.simple_spinner_item,
                        palette
                    ).also {
                        it.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
                    }
                    tag = palette
                    setSelection(uiConfiguration.requiredInt(control, "selectedIndex"))
                    onItemSelectedListener = itemListener { value ->
                        status.text = "$label: $value"
                        actionRuntime.invoke(controlId, "change")
                    }
                }, matchWrap())
            }
            "textField" -> {
                val label = uiConfiguration.requiredString(control, "label")
                val isPassword = uiConfiguration.optionalBoolean(control, "password")
                controlRoot.addView(fieldLabel(label), matchWrap())
                controlRoot.addView(EditText(activity).apply {
                    setSingleLine(true)
                    setText(uiConfiguration.stringValue(control, "value", allowEmpty = true))
                    hint = uiConfiguration.optionalString(control, "placeholder")
                    if (isPassword) {
                        transformationMethod = android.text.method.PasswordTransformationMethod.getInstance()
                    }
                    addTextChangedListener(SimpleTextWatcher { value ->
                        val hidden = transformationMethod is
                            android.text.method.PasswordTransformationMethod
                        status.text = if (hidden) "$label: hidden" else "$label: $value"
                        actionRuntime.invoke(controlId, "input")
                    })
                }, matchWrap())
            }
            "textArea" -> {
                val label = uiConfiguration.requiredString(control, "label")
                controlRoot.addView(fieldLabel(label), matchWrap())
                controlRoot.addView(EditText(activity).apply {
                    setSingleLine(false)
                    inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE
                    gravity = Gravity.TOP or Gravity.START
                    minLines = 3
                    setText(uiConfiguration.stringValue(control, "value", allowEmpty = true))
                    addTextChangedListener(SimpleTextWatcher { value ->
                        status.text = "$label: $value"
                        actionRuntime.invoke(controlId, "input")
                    })
                }, matchWrap())
            }
            "checkbox" -> controlRoot.addView(CheckBox(activity).apply {
                text = uiConfiguration.requiredString(control, "label")
                isChecked = uiConfiguration.requiredBoolean(control, "checked")
                setOnCheckedChangeListener { _, checked ->
                    status.text = "${control.getString("label")}: ${if (checked) "on" else "off"}"
                    actionRuntime.invoke(controlId, "change")
                }
            }, wrapWrap())
            "radioGroup" -> {
                val label = uiConfiguration.requiredString(control, "label")
                controlRoot.addView(fieldLabel(label), matchWrap())
                val options = uiConfiguration.stringArray(control.getJSONArray("options"))
                val selected = uiConfiguration.requiredInt(control, "selectedIndex")
                controlRoot.addView(RadioGroup(activity).apply {
                    orientation = RadioGroup.VERTICAL
                    options.forEachIndexed { index, option ->
                        addView(RadioButton(activity).apply {
                            text = option
                            id = View.generateViewId()
                            isChecked = index == selected
                        }, wrapWrap())
                    }
                    setOnCheckedChangeListener { group, checkedId ->
                        status.text =
                            "$label: ${group.findViewById<RadioButton>(checkedId).text}"
                        actionRuntime.invoke(controlId, "change")
                    }
                }, matchWrap())
            }
            "slider" -> {
                val label = uiConfiguration.requiredString(control, "label")
                val min = uiConfiguration.requiredInt(control, "min")
                val max = uiConfiguration.requiredInt(control, "max")
                val initialValue = uiConfiguration.requiredInt(control, "value")
                controlRoot.addView(fieldLabel("$label: $initialValue"), matchWrap())
                controlRoot.addView(SeekBar(activity).apply {
                    this.max = max - min
                    progress = initialValue - min
                    setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                        override fun onProgressChanged(
                            seekBar: SeekBar,
                            progress: Int,
                            fromUser: Boolean
                        ) {
                            status.text = "$label: ${progress + min}"
                            if (fromUser) actionRuntime.invoke(controlId, "input")
                        }

                        override fun onStartTrackingTouch(seekBar: SeekBar) = Unit
                        override fun onStopTrackingTouch(seekBar: SeekBar) = Unit
                    })
                }, matchWrap())
            }
            "list" -> {
                val label = uiConfiguration.requiredString(control, "label")
                controlRoot.addView(fieldLabel(label), matchWrap())
                val options = uiConfiguration.stringArray(control.getJSONArray("options"))
                controlRoot.addView(ListView(activity).apply {
                    adapter = optionAdapter(
                        options,
                        android.R.layout.simple_list_item_activated_1,
                        palette
                    )
                    tag = palette
                    choiceMode = ListView.CHOICE_MODE_SINGLE
                    layoutParams = LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        dp(160)
                    )
                    setItemChecked(uiConfiguration.requiredInt(control, "selectedIndex"), true)
                    setOnItemClickListener { _, _, position, _ ->
                        val item = adapter?.getItem(position)?.toString() ?: return@setOnItemClickListener
                        status.text = "$label: $item"
                        actionRuntime.invoke(controlId, "change")
                    }
                }, LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    dp(160)
                ))
            }
            "panel" -> {
                val label = uiConfiguration.requiredString(control, "label")
                controlRoot.addView(TextView(activity).apply {
                    text = label
                    textSize = 18f
                    setPadding(0, dp(4), 0, dp(8))
                }, matchWrap())
                val children = control.getJSONArray("children")
                for (index in 0 until children.length()) {
                    render(children.getString(index), controlRoot)
                }
            }
            "group" -> {
                controlRoot.orientation = uiConfiguration.orientationFor(
                    control.optJSONObject("layout"), activity.resources.configuration.screenWidthDp
                )
                val children = control.getJSONArray("children")
                for (index in 0 until children.length()) {
                    render(children.getString(index), controlRoot)
                }
            }
            "scrollPanel" -> {
                val label = uiConfiguration.requiredString(control, "label")
                controlRoot.addView(fieldLabel(label), matchWrap())
                val childList = LinearLayout(activity).apply {
                    orientation = LinearLayout.VERTICAL
                    setPadding(dp(8), dp(4), dp(8), dp(4))
                }
                val children = control.getJSONArray("children")
                for (index in 0 until children.length()) {
                    render(children.getString(index), childList)
                }
                controlRoot.addView(ScrollView(activity).apply {
                    isFillViewport = true
                    addView(childList, ViewGroup.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT
                    ))
                }, LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    dp(uiConfiguration.positiveInt(control, "height"))
                ))
            }
            else -> throw IllegalArgumentException("Unsupported control type.")
        }

        controlRoot.tag = controlViewMarker
        if (palette != null) {
            applyColors(controlRoot, palette.background, palette.foreground)
        }
        dom.register(controlId, controlRoot)
        val params = when {
            parent.orientation == LinearLayout.HORIZONTAL -> LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f
            )
            centered && type == "label" -> wrapWrap()
            else -> matchWrap()
        }
        parent.addView(controlRoot, params)
    }

    private fun paletteFor(control: JSONObject): OptionPalette? {
        val background = uiConfiguration.backgroundColor(control) ?: themeBackground
        val explicit = uiConfiguration.foregroundColor(control) ?: themeForeground
        val foreground = explicit ?: background?.let { uiConfiguration.contrastForeground(it) }
        if (background == null && foreground == null) return null
        return OptionPalette(background, foreground)
    }

    private fun optionAdapter(
        items: List<String>,
        layout: Int,
        palette: OptionPalette?
    ): ArrayAdapter<String> =
        if (palette == null) ArrayAdapter(activity, layout, items)
        else ColoredOptionAdapter(activity, layout, items, palette)

    private fun applyColors(view: View, background: Int?, foreground: Int?) {
        if (background != null) {
            if (view.background == null) {
                view.setBackgroundColor(background)
            } else {
                view.backgroundTintList = ColorStateList.valueOf(background)
            }
        }
        if (foreground != null && view is TextView) {
            view.setTextColor(foreground)
            if (view is EditText) view.setHintTextColor(foreground)
        }
        if (view is ViewGroup) {
            for (index in 0 until view.childCount) {
                val child = view.getChildAt(index)
                if (child.tag !== controlViewMarker) {
                    applyColors(child, background, foreground)
                }
            }
        }
    }

    private fun fieldLabel(text: String) = TextView(activity).apply {
        this.text = text
        setPadding(0, dp(8), 0, dp(2))
    }

    private fun matchWrap() = LinearLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.WRAP_CONTENT
    )

    private fun wrapWrap() = LinearLayout.LayoutParams(
        ViewGroup.LayoutParams.WRAP_CONTENT,
        ViewGroup.LayoutParams.WRAP_CONTENT
    )

    private fun dp(value: Int): Int =
        (value * activity.resources.displayMetrics.density).toInt()

    private fun itemListener(onSelected: (String) -> Unit) =
        object : android.widget.AdapterView.OnItemSelectedListener {
            override fun onItemSelected(
                parent: android.widget.AdapterView<*>,
                view: View?,
                position: Int,
                id: Long
            ) {
                onSelected(parent.getItemAtPosition(position).toString())
            }

            override fun onNothingSelected(parent: android.widget.AdapterView<*>) = Unit
        }
}
