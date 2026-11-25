package org.allwhither.starter

import android.app.Activity
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.LayerDrawable
import android.util.TypedValue
import android.view.Gravity
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
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
import org.json.JSONArray
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
        var percentHeightTarget: View? = null
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
                    if (control.optJSONObject("actions")?.has("enter") == true) {
                        imeOptions = EditorInfo.IME_ACTION_DONE
                        setOnEditorActionListener { _, actionId, event ->
                            val enterPressed =
                                actionId == EditorInfo.IME_ACTION_DONE ||
                                    (event?.keyCode == KeyEvent.KEYCODE_ENTER &&
                                        event.action == KeyEvent.ACTION_UP)
                            if (enterPressed) {
                                actionRuntime.invoke(controlId, "enter")
                            }
                            enterPressed
                        }
                    }
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
                val panelHeight = uiConfiguration.requiredDimension(control, "height")
                controlRoot.addView(fieldLabel(label), matchWrap())
                val childList = LinearLayout(activity).apply {
                    orientation = LinearLayout.VERTICAL
                    setPadding(dp(8), dp(4), dp(8), dp(4))
                }
                val children = control.getJSONArray("children")
                for (index in 0 until children.length()) {
                    render(children.getString(index), childList)
                }
                val scrollView = ScrollView(activity).apply {
                    isFillViewport = true
                    addView(childList, ViewGroup.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT
                    ))
                }
                controlRoot.addView(scrollView, LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    panelHeight.pixels?.let(::dp) ?: 0
                ))
                percentHeightTarget = scrollView
            }
            else -> throw IllegalArgumentException("Unsupported control type.")
        }

        controlRoot.tag = controlViewMarker
        if (palette != null) {
            applyColors(controlRoot, palette.background, palette.foreground)
        }
        if (control.has("fontSize")) {
            applyFontSize(controlRoot, uiConfiguration.positiveInt(control, "fontSize").toFloat())
        }
        applyBorder(
            control,
            if (type == "button") controlRoot.getChildAt(0) else controlRoot
        )
        dom.register(controlId, controlRoot)
        val widthDimension = uiConfiguration.optionalDimension(control, "width")
        val heightDimension = if (type == "scrollPanel") {
            null
        } else {
            uiConfiguration.optionalDimension(control, "height")
        }
        val isCentered = uiConfiguration.optionalBoolean(control, "center")
        val maxWidth = if (control.has("maxWidth")) {
            uiConfiguration.positiveInt(control, "maxWidth")
        } else {
            null
        }
        if (isCentered) controlRoot.gravity = Gravity.CENTER_HORIZONTAL
        val width = widthDimension?.pixels?.let(::dp)
            ?: if (widthDimension?.percent != null) 0
            else if (centered && type == "label") ViewGroup.LayoutParams.WRAP_CONTENT
            else if (parent.orientation == LinearLayout.HORIZONTAL && !isCentered) 0
            else if (isCentered) ViewGroup.LayoutParams.WRAP_CONTENT
            else ViewGroup.LayoutParams.MATCH_PARENT
        val height = heightDimension?.pixels?.let(::dp)
            ?: if (heightDimension?.percent != null) 0
            else ViewGroup.LayoutParams.WRAP_CONTENT
        val params = LinearLayout.LayoutParams(
            width,
            height,
            if (widthDimension == null && parent.orientation == LinearLayout.HORIZONTAL && !isCentered) 1f else 0f
        )
        if (isCentered) params.gravity = Gravity.CENTER_HORIZONTAL
        if (control.has("margin")) {
            val margin = dp(uiConfiguration.nonNegativeInt(control, "margin"))
            params.setMargins(margin, margin, margin, margin)
        }
        if (control.has("padding")) {
            val padding = dp(uiConfiguration.nonNegativeInt(control, "padding"))
            controlRoot.setPadding(padding, padding, padding, padding)
        }
        parent.addView(controlRoot, params)
        if (control.has("media")) {
            observeResponsiveVisibility(
                controlRoot, parent, control.getJSONArray("media")
            )
        }
        if (isCentered && parent.orientation == LinearLayout.HORIZONTAL) {
            centerInHorizontalParent(controlRoot, parent, params.leftMargin, params.rightMargin)
        }
        observePercentDimensions(controlRoot, parent, widthDimension, heightDimension, maxWidth)
        percentHeightTarget?.let { target ->
            observePercentDimensions(
                target,
                parent,
                null,
                uiConfiguration.requiredDimension(control, "height")
            )
        }
    }

    private fun centerInHorizontalParent(
        view: View,
        parent: LinearLayout,
        baseLeft: Int,
        baseRight: Int
    ) {
        fun update() {
            val params = view.layoutParams as? LinearLayout.LayoutParams ?: return
            val available = parent.width - parent.paddingLeft - parent.paddingRight
            if (available <= 0) return
            val extra = ((available - view.width - baseLeft - baseRight) / 2).coerceAtLeast(0)
            val left = baseLeft + extra
            val right = baseRight + extra
            if (params.leftMargin != left || params.rightMargin != right) {
                params.setMargins(left, params.topMargin, right, params.bottomMargin)
                view.layoutParams = params
            }
        }
        parent.addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ -> update() }
        parent.post { update() }
    }

    private fun observeResponsiveVisibility(
        view: View,
        parent: ViewGroup,
        media: JSONArray
    ) {
        fun update() {
            val widthDp = activity.resources.configuration.screenWidthDp
            val visibility = if (
                uiConfiguration.resolveControlVisibility(media, widthDp)
            ) View.VISIBLE else View.GONE
            if (view.visibility != visibility) view.visibility = visibility
        }
        parent.addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ -> update() }
        parent.post { update() }
    }

    private fun observePercentDimensions(
        view: View,
        parent: ViewGroup,
        width: UiDimension?,
        height: UiDimension?,
        maxWidth: Int? = null
    ) {
        if (width?.percent == null && height?.percent == null && maxWidth == null) return
        val initialParams = view.layoutParams as? LinearLayout.LayoutParams ?: return
        val initialWidth = initialParams.width
        val initialWeight = initialParams.weight
        val initialHorizontalMargins = initialParams.leftMargin + initialParams.rightMargin
        fun update() {
            val params = view.layoutParams as? LinearLayout.LayoutParams ?: return
            var changed = false
            val availableWidth = parent.width - parent.paddingLeft - parent.paddingRight
            if (width?.percent != null) {
                if (availableWidth > 0) {
                    val target = width.resolve(availableWidth)
                    if (params.width != target) {
                        params.width = target
                        changed = true
                    }
                }
            }
            if (maxWidth != null && availableWidth > 0) {
                val available = (availableWidth - initialHorizontalMargins).coerceAtLeast(1)
                val requestedWidth = when {
                    width?.pixels != null -> dp(width.pixels)
                    width?.percent != null -> width.resolve(availableWidth)
                    initialWeight > 0f || initialWidth == ViewGroup.LayoutParams.MATCH_PARENT ->
                        available
                    else -> initialWidth
                }
                val target = minOf(dp(maxWidth), available, requestedWidth.takeIf { it > 0 } ?: available)
                if (params.width != target) {
                    params.width = target
                    changed = true
                }
                if (params.weight != 0f) {
                    params.weight = 0f
                    changed = true
                }
            }
            if (height?.percent != null) {
                val available = parent.height - parent.paddingTop - parent.paddingBottom
                if (available > 0) {
                    val target = height.resolve(available)
                    if (params.height != target) {
                        params.height = target
                        changed = true
                    }
                }
            }
            if (changed) view.layoutParams = params
        }
        parent.addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ -> update() }
        parent.post { update() }
    }

    private fun paletteFor(control: JSONObject): OptionPalette? {
        val background = uiConfiguration.backgroundColor(control) ?: themeBackground
        val explicit = uiConfiguration.foregroundColor(control) ?: themeForeground
        val foreground = explicit ?: background?.let { uiConfiguration.contrastForeground(it) }
        if (background == null && foreground == null) return null
        return OptionPalette(background, foreground)
    }

    private fun applyBorder(control: JSONObject, view: View) {
        if (!control.has("borderStyle") &&
            !control.has("borderColor") &&
            !control.has("borderWidth")) return
        val style = if (control.has("borderStyle")) {
            uiConfiguration.optionalString(control, "borderStyle")
        } else {
            "solid"
        }
        if (style == "none") return
        val borderUnits = if (control.has("borderWidth")) {
            uiConfiguration.nonNegativeInt(control, "borderWidth")
        } else {
            1
        }
        val width = if (borderUnits == 0) 0 else dp(borderUnits).coerceAtLeast(1)
        if (width == 0) return
        val color = uiConfiguration.borderColor(control) ?: Color.BLACK
        val stroke = GradientDrawable().apply {
            setColor(Color.TRANSPARENT)
            when (style) {
                "dashed" -> setStroke(width, color, width * 4f, width * 2f)
                "dotted" -> setStroke(width, color, width.toFloat(), width.toFloat())
                else -> setStroke(width, color)
            }
        }
        val background = view.background
        view.background = if (background == null) {
            stroke
        } else {
            LayerDrawable(arrayOf(background, stroke))
        }
    }

    private fun applyFontSize(view: View, sizeInPixels: Float) {
        if (view is TextView) {
            view.setTextSize(TypedValue.COMPLEX_UNIT_PX, sizeInPixels)
        }
        if (view is ViewGroup) {
            for (index in 0 until view.childCount) {
                val child = view.getChildAt(index)
                if (child.tag !== controlViewMarker) {
                    applyFontSize(child, sizeInPixels)
                }
            }
        }
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
