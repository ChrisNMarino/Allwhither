package org.omni.starter

import android.content.Context
import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.TextView

internal data class OptionPalette(val background: Int?, val foreground: Int?)

internal class ColoredOptionAdapter(
    context: Context,
    resource: Int,
    items: List<String>,
    private val palette: OptionPalette
) : ArrayAdapter<String>(context, resource, items) {
    override fun getView(position: Int, convertView: View?, parent: ViewGroup): View =
        paint(super.getView(position, convertView, parent), dropdown = false)

    override fun getDropDownView(position: Int, convertView: View?, parent: ViewGroup): View =
        paint(super.getDropDownView(position, convertView, parent), dropdown = true)

    private fun paint(view: View, dropdown: Boolean): View {
        if (view is TextView) {
            palette.foreground?.let(view::setTextColor)
            if (dropdown) palette.background?.let(view::setBackgroundColor)
        }
        return view
    }
}
