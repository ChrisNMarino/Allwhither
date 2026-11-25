export class UiDom {
  constructor(controlDefinitions, statusElement) {
    this.controlDefinitions = controlDefinitions;
    this.statusElement = statusElement;
    this.elements = new Map();
    this.mountParent = null;
    this.renderMounted = null;
    this.updating = false;
  }

  setMount(parent, render) {
    this.mountParent = parent;
    this.renderMounted = render;
  }

  register(id, element) {
    this.elements.set(id, element);
  }

  getText(id) {
    const definition = this.definition(id);
    const element = this.element(id);
    if (definition.type === "label" || definition.type === "button") {
      return element.textContent;
    }
    if (definition.type === "checkbox") {
      return element.lastChild.textContent;
    }
    if (definition.type === "radioGroup") {
      return element.querySelector("legend").textContent;
    }
    if (definition.type === "scrollPanel") {
      return element.querySelector("h2").textContent;
    }
    if (definition.type === "panel") {
      const titleId = this.panelTitleId(id);
      return titleId ? this.getText(titleId) : "";
    }
    return element.querySelector(":scope > span")?.textContent ?? definition.label ?? "";
  }

  setText(id, text) {
    const type = this.definition(id).type;
    const element = this.element(id);
    if (type === "label" || type === "button") {
      element.textContent = String(text);
      return;
    }
    if (type === "checkbox") {
      element.lastChild.textContent = String(text);
      return;
    }
    if (type === "radioGroup") {
      element.querySelector("legend").textContent = String(text);
      return;
    }
    if (type === "scrollPanel") {
      element.querySelector("h2").textContent = String(text);
      return;
    }
    if (type === "panel") {
      const titleId = this.panelTitleId(id);
      if (!titleId) {
        throw new Error(`Panel '${id}' has no leading label control.`);
      }
      this.setText(titleId, text);
      return;
    }
    const caption = element.querySelector(":scope > span");
    if (caption) {
      caption.textContent = String(text);
      return;
    }
    throw new Error(`Control '${id}' does not expose writable text.`);
  }

  getValue(id) {
    const definition = this.definition(id);
    const element = this.element(id);
    if (["button", "label", "checkbox", "scrollPanel"].includes(definition.type)) {
      return null;
    }
    if (definition.type === "radioGroup") {
      return element.querySelector("input[type=radio]:checked")?.value ?? null;
    }
    return element.querySelector("input,select,textarea")?.value ?? null;
  }

  setValue(id, value) {
    const definition = this.definition(id);
    const element = this.element(id);
    if (["button", "label", "checkbox", "scrollPanel"].includes(definition.type)) {
      throw new Error(`Control '${id}' does not expose a value.`);
    }
    if (definition.type === "radioGroup") {
      const option = [...element.querySelectorAll("input[type=radio]")]
        .find((input) => input.value === String(value));
      if (!option) {
        throw new Error(`Value '${value}' is not an option of control '${id}'.`);
      }
      option.checked = true;
      return;
    }
    const input = element.querySelector("input,select,textarea");
    if (!input) {
      throw new Error(`Control '${id}' does not expose a value.`);
    }
    input.value = String(value);
    if (input.type === "range") {
      const output = element.querySelector("output");
      if (output) output.value = input.value;
    }
  }

  setVisible(id, visible) {
    if (typeof visible !== "boolean") {
      throw new Error("visible must be a boolean.");
    }
    this.element(id).hidden = !visible;
  }

  getChecked(id) {
    if (this.definition(id).type !== "checkbox") {
      throw new Error(`Control '${id}' does not expose a checked state.`);
    }
    return this.element(id).querySelector("input[type=checkbox]").checked;
  }

  setChecked(id, checked) {
    if (this.definition(id).type !== "checkbox") {
      throw new Error(`Control '${id}' does not expose a checked state.`);
    }
    this.element(id).querySelector("input[type=checkbox]").checked = Boolean(checked);
  }

  setSecure(id, secure) {
    const input = this.element(id).querySelector("input");
    if (!input) {
      throw new Error(`Control '${id}' does not expose a secure state.`);
    }
    input.type = secure ? "password" : "text";
  }

  getSecure(id) {
    const input = this.element(id).querySelector("input");
    return input ? input.type === "password" : false;
  }

  setStatus(text) {
    this.statusElement.textContent = String(text);
  }

  addPanel(id) {
    const definition = this.definition(id);
    if (definition.type !== "panel") {
      throw new Error(`Control '${id}' is not a panel.`);
    }
    if (this.elements.has(id)) {
      throw new Error(`Panel '${id}' is already shown.`);
    }
    if (!this.mountParent || !this.renderMounted) {
      throw new Error("The window is not ready to add a panel.");
    }
    this.mountParent.append(this.renderMounted(id));
  }

  removePanel(id) {
    const definition = this.definition(id);
    if (definition.type !== "panel") {
      throw new Error(`Control '${id}' is not a panel.`);
    }
    const element = this.elements.get(id);
    if (!element) {
      throw new Error(`Panel '${id}' is not shown.`);
    }
    this.unregisterTree(id);
    element.remove();
  }

  setOptions(id, options, selectedIndex) {
    if (!Array.isArray(options) || options.some((option) =>
      typeof option !== "string" || option.trim() === "")) {
      throw new Error("options must be an array of non-empty strings.");
    }
    const definition = this.definition(id);
    if (definition.type !== "list" && definition.type !== "comboBox") {
      throw new Error(`Control '${id}' does not expose options.`);
    }
    const select = this.element(id).querySelector("select");
    if (!select) {
      throw new Error(`Control '${id}' does not expose options.`);
    }
    const indexProvided = arguments.length >= 3 && selectedIndex !== undefined;
    let index;
    if (options.length === 0) {
      if (indexProvided && selectedIndex !== -1) {
        throw new Error("selectedIndex is out of range.");
      }
      index = -1;
    } else if (!indexProvided) {
      index = 0;
    } else if (!Number.isInteger(selectedIndex) ||
        selectedIndex < 0 || selectedIndex >= options.length) {
      throw new Error("selectedIndex is out of range.");
    } else {
      index = selectedIndex;
    }
    this.updating = true;
    try {
      select.replaceChildren(...options.map((option) => {
        const element = document.createElement("option");
        element.textContent = option;
        element.style.color = select.style.color;
        element.style.backgroundColor = select.style.backgroundColor;
        return element;
      }));
      if (definition.type === "list") {
        select.size = Math.max(options.length, 2);
      }
      select.selectedIndex = index;
    } finally {
      this.updating = false;
    }
  }

  getSelectedIndex(id) {
    const definition = this.definition(id);
    if (definition.type !== "list" && definition.type !== "comboBox") {
      throw new Error(`Control '${id}' does not expose a selection index.`);
    }
    const select = this.element(id).querySelector("select");
    if (!select) {
      throw new Error(`Control '${id}' does not expose a selection index.`);
    }
    return select.selectedIndex;
  }

  unregisterTree(id) {
    const definition = this.controlDefinitions[id];
    if (definition && (definition.type === "panel" || definition.type === "scrollPanel")) {
      for (const childId of definition.children) {
        this.unregisterTree(childId);
      }
    }
    this.elements.delete(id);
  }

  panelTitleId(id) {
    const firstChildId = this.definition(id).children?.[0];
    return typeof firstChildId === "string" &&
      this.definition(firstChildId).type === "label"
      ? firstChildId
      : null;
  }

  definition(id) {
    if (!Object.hasOwn(this.controlDefinitions, id)) {
      throw new Error(`Unknown control id: ${id}`);
    }
    return this.controlDefinitions[id];
  }

  element(id) {
    const element = this.elements.get(id);
    if (!element) {
      throw new Error(`Control '${id}' has not been rendered.`);
    }
    return element;
  }
}
