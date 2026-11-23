import { effectiveColors } from "./colors.js";

function labeledControl(label, control) {
  const wrapper = document.createElement("label");
  wrapper.className = "field";
  const caption = document.createElement("span");
  caption.textContent = label;
  wrapper.append(caption, control);
  return wrapper;
}

function paintColors(element, background, foreground, isRoot) {
  if (!isRoot && element.dataset.omniControl === "true") {
    return;
  }
  if (background) {
    element.style.backgroundColor = background;
  }
  if (foreground) {
    element.style.color = foreground;
    element.style.caretColor = foreground;
  }
  for (const child of element.children) {
    paintColors(child, background, foreground, false);
  }
}

export function renderControl(controlId, controls, status, actionRuntime, dom, theme = {}) {
  const control = controls[controlId];
  const element = renderControlContent(
    controlId, controls, status, actionRuntime, dom, theme,
  );
  element.dataset.omniControl = "true";
  dom.register(controlId, element);
  const colors = effectiveColors(control, theme);
  if (colors.background || colors.foreground) {
    if (colors.foreground) {
      element.style.setProperty("--omni-placeholder", colors.foreground);
      element.classList.add("omni-colored");
    }
    paintColors(element, colors.background, colors.foreground, true);
  }
  for (const [eventType, action] of Object.entries(control.actions ?? {})) {
    const targets = actionTargets(control.type, element);
    for (const target of targets[eventType] ?? []) {
      target.addEventListener(eventType, () => {
        actionRuntime.invoke(controlId, eventType);
      });
    }
    if (!targets[eventType]?.length && action) {
      throw new Error(`No '${eventType}' event target exists for control '${controlId}'.`);
    }
  }
  return element;
}

function actionTargets(type, element) {
  const selectors = {
    button: { click: "button" },
    comboBox: { change: "select" },
    textField: { input: "input" },
    textArea: { input: "textarea" },
    checkbox: { change: "input[type=checkbox]" },
    radioGroup: { change: "input[type=radio]" },
    slider: { input: "input[type=range]" },
    list: { change: "select" },
  };
  return Object.fromEntries(
    Object.entries(selectors[type] ?? {}).map(([event, selector]) => [
      event,
      element.matches(selector)
        ? [element]
        : [...element.querySelectorAll(selector)],
    ]),
  );
}

function renderControlContent(controlId, controls, status, actionRuntime, dom, theme) {
  const control = controls[controlId];
  switch (control.type) {
    case "label": {
      const label = document.createElement("p");
      label.className = "control-label";
      label.textContent = control.text;
      return label;
    }
    case "button": {
      const button = document.createElement("button");
      button.type = "button";
      button.textContent = control.text;
      button.addEventListener("click", () => {
        button.textContent = control.clickedText;
        status.textContent = control.clickedText;
      });
      return button;
    }
    case "comboBox": {
      const select = document.createElement("select");
      control.options.forEach((option) => {
        const element = document.createElement("option");
        element.textContent = option;
        select.append(element);
      });
      select.selectedIndex = control.selectedIndex;
      select.addEventListener("change", () => {
        status.textContent = `${control.label}: ${select.value}`;
      });
      return labeledControl(control.label, select);
    }
    case "textField": {
      const input = document.createElement("input");
      input.type = control.password ? "password" : "text";
      input.value = control.value;
      input.placeholder = control.placeholder ?? "";
      input.addEventListener("input", () => {
        status.textContent = input.type === "password"
          ? `${control.label}: hidden`
          : `${control.label}: ${input.value}`;
      });
      return labeledControl(control.label, input);
    }
    case "textArea": {
      const input = document.createElement("textarea");
      input.value = control.value;
      input.rows = 4;
      input.addEventListener("input", () => {
        status.textContent = `${control.label}: ${input.value}`;
      });
      return labeledControl(control.label, input);
    }
    case "checkbox": {
      const label = document.createElement("label");
      label.className = "check-field";
      const input = document.createElement("input");
      input.type = "checkbox";
      input.checked = control.checked;
      input.addEventListener("change", () => {
        status.textContent = `${control.label}: ${input.checked ? "on" : "off"}`;
      });
      label.append(input, document.createTextNode(control.label));
      return label;
    }
    case "radioGroup": {
      const group = document.createElement("fieldset");
      const legend = document.createElement("legend");
      legend.textContent = control.label;
      group.append(legend);
      const name = `radio-${nextRadioId++}`;
      control.options.forEach((option, index) => {
        const label = document.createElement("label");
        label.className = "radio-option";
        const input = document.createElement("input");
        input.type = "radio";
        input.name = name;
        input.value = option;
        input.checked = index === control.selectedIndex;
        input.addEventListener("change", () => {
          status.textContent = `${control.label}: ${option}`;
        });
        label.append(input, document.createTextNode(option));
        group.append(label);
      });
      return group;
    }
    case "slider": {
      const input = document.createElement("input");
      input.type = "range";
      input.min = control.min;
      input.max = control.max;
      input.value = control.value;
      const value = document.createElement("output");
      value.value = control.value;
      input.addEventListener("input", () => {
        value.value = input.value;
        status.textContent = `${control.label}: ${input.value}`;
      });
      const field = labeledControl(control.label, input);
      field.append(value);
      return field;
    }
    case "list": {
      const select = document.createElement("select");
      select.size = Math.min(control.options.length, 4);
      control.options.forEach((option) => {
        const element = document.createElement("option");
        element.textContent = option;
        select.append(element);
      });
      select.selectedIndex = control.selectedIndex;
      select.addEventListener("change", () => {
        status.textContent = `${control.label}: ${select.value}`;
      });
      return labeledControl(control.label, select);
    }
    case "panel": {
      const panel = document.createElement("section");
      panel.className = "view-panel";
      const heading = document.createElement("h2");
      heading.textContent = control.label;
      const children = document.createElement("div");
      children.className = "control-list";
      control.children.forEach((childId) => {
        children.append(renderControl(
          childId, controls, status, actionRuntime, dom, theme,
        ));
      });
      panel.append(heading, children);
      return panel;
    }
    case "scrollPanel": {
      const panel = document.createElement("section");
      panel.className = "scroll-panel";
      panel.style.height = `${control.height}px`;
      const heading = document.createElement("h2");
      heading.textContent = control.label;
      const children = document.createElement("div");
      children.className = "control-list";
      control.children.forEach((childId) => {
        children.append(renderControl(
          childId, controls, status, actionRuntime, dom, theme,
        ));
      });
      panel.append(heading, children);
      return panel;
    }
    default:
      throw new Error(`Unsupported control type: ${control.type}`);
  }
}

let nextRadioId = 0;

export function renderMenus(menus, status, menuBar) {
  menus.forEach((menu) => {
    const dropdown = document.createElement("details");
    dropdown.className = "menu";
    const summary = document.createElement("summary");
    summary.textContent = menu.label;
    const items = document.createElement("div");
    items.className = "menu-items";
    menu.items.forEach((item) => {
      const button = document.createElement("button");
      button.type = "button";
      button.textContent = item.text;
      button.addEventListener("click", () => {
        status.textContent = item.message;
        dropdown.open = false;
      });
      items.append(button);
    });
    dropdown.append(summary, items);
    menuBar.append(dropdown);
  });
}
