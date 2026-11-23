import { NativeApi } from "./native-api.js";
import { normalizeResourcePath } from "./resource-codec.js";

function publishHostObject(name, value) {
  Object.defineProperty(globalThis, name, {
    value,
    writable: false,
    enumerable: true,
    configurable: true,
  });
}

function actionReferences(controls) {
  return Object.values(controls).flatMap((control) =>
    Object.values(control.actions ?? {}),
  );
}

export class ActionRuntime {
  constructor(controls, dom, resources, scripts = []) {
    this.controls = controls;
    this.dom = dom;
    this.api = new NativeApi();
    this.resources = resources;
    this.scripts = scripts;
    this.loadedFiles = new Set();
    publishHostObject("dom", this.dom);
    publishHostObject("api", this.api);
  }

  async prepare() {
    for (const file of this.scripts) {
      this.loadFile(file, false);
    }
    for (const action of actionReferences(this.controls)) {
      this.loadFile(action.file, true);
      if (typeof globalThis[action.function] !== "function") {
        throw new Error(
          `Action function '${action.function}' was not defined by ${action.file}.`,
        );
      }
    }
  }

  invoke(controlId, eventType) {
    if (this.dom.updating) return;
    const action = this.controls[controlId].actions?.[eventType];
    if (!action) return;
    const handler = globalThis[action.function];
    if (typeof handler !== "function") {
      throw new Error(`Action function '${action.function}' is unavailable.`);
    }
    try {
      handler(this.dom, {
        type: eventType,
        targetId: controlId,
        text: this.dom.getText(controlId),
        value: this.dom.getValue(controlId),
        checked: this.controls[controlId].type === "checkbox"
          ? this.dom.getChecked(controlId)
          : null,
      });
    } catch (error) {
      this.dom.setStatus(`JavaScript action failed: ${error.message}`);
      console.error(error);
    }
  }

  loadFile(file, action = true) {
    const normalized = normalizeResourcePath(file);
    if (!normalized.toLowerCase().endsWith(".js")) {
      throw new Error(
        action
          ? "Action files must be relative .js paths without parent segments."
          : "Script files must be relative .js paths without parent segments.",
      );
    }
    if (this.loadedFiles.has(normalized)) return;
    const source = this.resources.readText(normalized);
    const script = document.createElement("script");
    script.textContent = source;
    document.head.append(script);
    this.loadedFiles.add(normalized);
  }
}
