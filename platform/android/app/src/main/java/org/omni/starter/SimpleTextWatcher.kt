package org.omni.starter

import android.text.Editable
import android.text.TextWatcher

internal class SimpleTextWatcher(
    private val onChange: (String) -> Unit
) : TextWatcher {
    override fun beforeTextChanged(text: CharSequence?, start: Int, count: Int, after: Int) = Unit

    override fun onTextChanged(text: CharSequence?, start: Int, before: Int, count: Int) {
        onChange(text?.toString().orEmpty())
    }

    override fun afterTextChanged(text: Editable?) = Unit
}
