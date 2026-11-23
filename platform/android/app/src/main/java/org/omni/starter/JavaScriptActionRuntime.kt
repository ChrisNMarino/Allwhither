package org.omni.starter

import org.json.JSONArray
import org.json.JSONObject
import org.mozilla.javascript.Context
import org.mozilla.javascript.Function
import org.mozilla.javascript.RhinoException
import org.mozilla.javascript.Scriptable
import org.mozilla.javascript.ScriptableObject

internal class JavaScriptActionRuntime(
    private val activity: MainActivity,
    private val controls: JSONObject,
    private val dom: UiDom,
    private val scripts: JSONArray = JSONArray()
) {
    private val api = NativeApi(activity)
    private lateinit var scope: Scriptable
    private val actions = mutableMapOf<Pair<String, String>, Function>()
    private val loadedFiles = mutableSetOf<String>()
    private var invoking = false

    init {
        val context = Context.enter()
        try {
            context.optimizationLevel = -1
            scope = context.initStandardObjects()
            api.attach(scope)
            val apiObject = Context.javaToJS(api, scope)
            ScriptableObject.putProperty(scope, "dom", Context.javaToJS(dom, scope))
            ScriptableObject.putProperty(scope, "api", apiObject)
            for (index in 0 until scripts.length()) {
                val file = scripts.get(index)
                if (file !is String || file.isBlank()) {
                    throw IllegalArgumentException("scripts must contain non-empty paths.")
                }
                evaluate(context, file)
            }
            val ids = controls.keys()
            while (ids.hasNext()) {
                val control = controls.getJSONObject(ids.next())
                val actionSet = control.optJSONObject("actions") ?: continue
                val eventTypes = actionSet.keys()
                while (eventTypes.hasNext()) {
                    val action = actionSet.getJSONObject(eventTypes.next())
                    load(context, action.getString("file"), action.getString("function"))
                }
            }
        } finally {
            Context.exit()
        }
    }

    fun invoke(controlId: String, eventType: String) {
        if (invoking || !dom.isRegistered(controlId)) return
        val control = controls.getJSONObject(controlId)
        val action = control.optJSONObject("actions")?.optJSONObject(eventType) ?: return
        val file = action.getString("file")
        val functionName = action.getString("function")
        val function = actions[file to functionName] ?: return
        val context = Context.enter()
        invoking = true
        try {
            val event = context.newObject(scope)
            ScriptableObject.putProperty(event, "type", eventType)
            ScriptableObject.putProperty(event, "targetId", controlId)
            ScriptableObject.putProperty(event, "text", dom.getText(controlId))
            ScriptableObject.putProperty(
                event,
                "value",
                Context.javaToJS(dom.getValue(controlId), scope)
            )
            ScriptableObject.putProperty(
                event,
                "checked",
                Context.javaToJS(
                    if (control.getString("type") == "checkbox") dom.getChecked(controlId)
                    else null,
                    scope
                )
            )
            function.call(context, scope, scope, arrayOf(dom, event))
        } catch (exception: RhinoException) {
            dom.setStatus("JavaScript action failed: ${exception.message}")
        } finally {
            invoking = false
            Context.exit()
        }
    }

    private fun load(context: Context, file: String, functionName: String) {
        if (actions.containsKey(file to functionName)) return
        evaluate(context, file)
        val function = scope.get(functionName, scope) as? Function
            ?: throw IllegalArgumentException(
                "Action function '$functionName' was not defined by $file."
            )
        actions[file to functionName] = function
    }

    private fun evaluate(context: Context, file: String) {
        if (!loadedFiles.add(file)) return
        val source = EmbeddedResources.readText(file)
        context.evaluateString(scope, source, file, 1, null)
    }
}
