package org.allwhither.starter

import android.app.Activity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.PopupMenu
import android.widget.TextView
import org.json.JSONArray
import org.json.JSONObject

internal class MenuRenderer(
    private val activity: Activity,
    private val status: TextView,
    private val uiConfiguration: UiConfiguration
) {
    fun addTo(menuBar: LinearLayout, menus: JSONArray, chrome: ThemeColors? = null) {
        for (menuIndex in 0 until menus.length()) {
            addMenu(menuBar, menus.getJSONObject(menuIndex), chrome)
        }
    }

    private fun addMenu(menuBar: LinearLayout, menu: JSONObject, chrome: ThemeColors?) {
        val popupButton = Button(activity).apply {
            text = uiConfiguration.requiredString(menu, "label")
            chrome?.foreground?.let { setTextColor(it) }
            chrome?.background?.let { setBackgroundColor(it) }
        }
        val popup = PopupMenu(activity, popupButton)
        val items = menu.getJSONArray("items")
        for (index in 0 until items.length()) {
            val item = items.getJSONObject(index)
            popup.menu.add(uiConfiguration.requiredString(item, "text"))
                .setOnMenuItemClickListener {
                    status.text = uiConfiguration.requiredString(item, "message")
                    true
                }
        }
        popupButton.setOnClickListener { popup.show() }
        menuBar.addView(popupButton, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.WRAP_CONTENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
        ))
    }
}
