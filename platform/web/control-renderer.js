import { effectiveColors } from "./colors.js";
import { resolveControlVisibility, resolveLayoutType } from "./ui-config.js";

function labeledControl(label, control) {
  const wrapper = document.createElement("label");
  wrapper.className = "field";
  const caption = document.createElement("span");
  caption.textContent = label;
  wrapper.append(caption, control);
  return wrapper;
}

function paintColors(element, background, foreground, isRoot) {
  if (!isRoot && element.dataset.allwhitherControl === "true") {
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
  if (control.margin !== undefined) {
    element.style.margin = `${control.margin}px`;
  }
  if (control.padding !== undefined) {
    element.style.padding = `${control.padding}px`;
  }
  if (control.fontSize !== undefined) {
    element.style.fontSize = `${control.fontSize}px`;
  }
  if (control.borderStyle !== undefined || control.borderColor !== undefined ||
      control.borderWidth !== undefined) {
    element.style.borderStyle = control.borderStyle ?? "solid";
    element.style.borderColor = control.borderColor ?? "#000000";
    element.style.borderWidth = `${control.borderWidth ?? 1}px`;
  }
  if (control.width !== undefined) {
    element.style.width = dimensionCss(control.width);
    element.style.flex = "none";
  }
  if (control.maxWidth !== undefined) {
    element.style.maxWidth = `${control.maxWidth}px`;
  }
  if (control.height !== undefined && control.type !== "scrollPanel") {
    element.style.height = dimensionCss(control.height);
  }
  if (control.center) {
    element.dataset.centerControl = "true";
    element.style.flex = "none";
    element.style.alignSelf = "center";
  }
  element.dataset.allwhitherControl = "true";
  dom.register(controlId, element);
  if (control.media !== undefined) {
    trackResponsiveVisibility(element, control.media);
  }
  const colors = effectiveColors(control, theme);
  if (colors.background || colors.foreground) {
    if (colors.foreground) {
      element.style.setProperty("--allwhither-placeholder", colors.foreground);
      element.classList.add("allwhither-colored");
    }
    paintColors(element, colors.background, colors.foreground, true);
  }
  for (const [eventType, action] of Object.entries(control.actions ?? {})) {
    const targets = actionTargets(control.type, element);
    for (const target of targets[eventType] ?? []) {
      if (eventType === "enter") {
        target.addEventListener("keydown", (event) => {
          if (event.key === "Enter") {
            event.preventDefault();
            actionRuntime.invoke(controlId, eventType);
          }
        });
      } else {
        target.addEventListener(eventType, () => {
          actionRuntime.invoke(controlId, eventType);
        });
      }
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
    textField: { input: "input", enter: "input" },
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
      if (control.autocomplete !== undefined) {
        input.autocomplete = control.autocomplete;
      }
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
      const slider = document.createElement("span");
      slider.className = "slider-control";
      slider.append(input, value);
      return labeledControl(control.label, slider);
    }
    case "list": {
      const select = document.createElement("select");
      select.size = Math.max(control.options.length, 2);
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
      const children = document.createElement("div");
      children.className = "control-list";
      control.children.forEach((childId) => {
        children.append(renderControl(
          childId, controls, status, actionRuntime, dom, theme,
        ));
      });
      panel.append(children);
      return panel;
    }
    case "group": {
      const group = document.createElement("div");
      group.className = "control-list control-group";
      control.children.forEach((childId) => {
        group.append(renderControl(
          childId, controls, status, actionRuntime, dom, theme,
        ));
      });
      trackResponsiveLayout(group, control.layout ?? { type: "vertical" });
      return group;
    }
    case "scrollPanel": {
      const panel = document.createElement("section");
      panel.className = "scroll-panel";
      panel.style.height = dimensionCss(control.height);
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

function dimensionCss(value) {
  return typeof value === "number" ? `${value}px` : value;
}

let nextRadioId = 0;

const responsiveElements = [];
const responsiveControls = [];

export function trackResponsiveLayout(element, layout) {
  responsiveElements.push({ element, layout });
  applyResponsiveLayout({ element, layout }, window.innerWidth);
}

function trackResponsiveVisibility(element, media) {
  const responsiveControl = { element, media };
  responsiveControls.push(responsiveControl);
  applyResponsiveVisibility(responsiveControl, window.innerWidth);
}

function applyResponsiveVisibility({ element, media }, width) {
  element.style.display = resolveControlVisibility(media, width) ? "" : "none";
}

function applyResponsiveLayout({ element, layout }, width) {
  const type = resolveLayoutType(layout, width);
  element.classList.toggle("horizontal", type === "horizontal");
  element.classList.toggle("centered", type === "center");
  for (const child of element.children) {
    if (child.dataset.centerControl !== "true") continue;
    child.style.alignSelf = type === "horizontal" ? "" : "center";
    child.style.marginInlineStart = type === "horizontal" ? "auto" : "";
    child.style.marginInlineEnd = type === "horizontal" ? "auto" : "";
  }
}

export function updateResponsiveLayouts() {
  const width = window.innerWidth;
  for (let i = responsiveElements.length - 1; i >= 0; i--) {
    if (!responsiveElements[i].element.isConnected && responsiveElements[i].seen) {
      responsiveElements.splice(i, 1);
      continue;
    }
    responsiveElements[i].seen = responsiveElements[i].element.isConnected;
    applyResponsiveLayout(responsiveElements[i], width);
  }
  for (let i = responsiveControls.length - 1; i >= 0; i--) {
    if (!responsiveControls[i].element.isConnected && responsiveControls[i].seen) {
      responsiveControls.splice(i, 1);
      continue;
    }
    responsiveControls[i].seen = responsiveControls[i].element.isConnected;
    applyResponsiveVisibility(responsiveControls[i], width);
  }
}

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
