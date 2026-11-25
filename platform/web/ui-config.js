import { normalizeResourcePath, resolveResourcePath } from "./resource-codec.js";

function requireString(value, name) {
  if (typeof value !== "string" || value.trim() === "") {
    throw new Error(`${name} must be a non-empty string.`);
  }
  return value;
}

function isInt32(value) {
  return Number.isInteger(value) && value >= -2147483648 && value <= 2147483647;
}

function requireOptions(value, name) {
  if (!Array.isArray(value) || value.length === 0) {
    throw new Error(`${name} must contain at least one option.`);
  }
  value.forEach((option, index) => requireString(option, `${name}[${index}]`));
  return value;
}

function validateOptionalColor(value, name) {
  if (value !== undefined && (typeof value !== "string" || !/^#[\da-f]{6}$/i.test(value))) {
    throw new Error(`${name} must be a #RRGGBB color.`);
  }
}

function validateOptionalMargin(control) {
  if (control.margin !== undefined) {
    if (!isInt32(control.margin) || control.margin < 0) {
      throw new Error("control.margin must be a non-negative integer.");
    }
  }
}

function validateOptionalPadding(control) {
  if (control.padding !== undefined) {
    if (!isInt32(control.padding) || control.padding < 0) {
      throw new Error("control.padding must be a non-negative integer.");
    }
  }
}

function validateOptionalBorder(control) {
  if (control.borderStyle !== undefined &&
      !["solid", "dashed", "dotted", "none"].includes(control.borderStyle)) {
    throw new Error("control.borderStyle must be solid, dashed, dotted, or none.");
  }
  validateOptionalColor(control.borderColor, "control.borderColor");
  if (control.borderWidth !== undefined &&
      (!isInt32(control.borderWidth) || control.borderWidth < 0)) {
    throw new Error("control.borderWidth must be a non-negative integer.");
  }
}

function validateDimension(value, name) {
  if (isInt32(value) && value > 0) return;
  if (typeof value === "string" && /^\d+(?:\.\d+)?%$/.test(value)) {
    const percent = Number(value.slice(0, -1));
    if (percent > 0 && percent <= 100) return;
  }
  throw new Error(`${name} must be a positive pixel value or a percentage greater than 0% and at most 100%.`);
}

function validateControl(control) {
  requireString(control.type, "control.type");
  validateOptionalColor(control.backgroundColor, "control.backgroundColor");
  validateOptionalColor(control.foregroundColor, "control.foregroundColor");
  validateOptionalMargin(control);
  validateOptionalPadding(control);
  validateOptionalBorder(control);
  if (control.fontSize !== undefined &&
      (!isInt32(control.fontSize) || control.fontSize <= 0)) {
    throw new Error("control.fontSize must be a positive integer.");
  }
  if (control.center !== undefined && typeof control.center !== "boolean") {
    throw new Error("control.center must be a boolean.");
  }
  for (const dimension of ["width", "height"]) {
    if (control[dimension] !== undefined) {
      validateDimension(control[dimension], `control.${dimension}`);
    }
  }
  if (control.maxWidth !== undefined &&
      (!isInt32(control.maxWidth) || control.maxWidth <= 0)) {
    throw new Error("control.maxWidth must be a positive integer.");
  }
  if (control.media !== undefined) {
    validateControlMedia(control.media);
  }
  validateActions(control);

  switch (control.type) {
    case "label":
      requireString(control.text, "label.text");
      break;
    case "button":
      requireString(control.text, "button.text");
      requireString(control.clickedText, "button.clickedText");
      break;
    case "comboBox":
    case "radioGroup":
    case "list": {
      requireString(control.label, `${control.type}.label`);
      const options = requireOptions(control.options, `${control.type}.options`);
      if (
        !isInt32(control.selectedIndex) ||
        control.selectedIndex < 0 ||
        control.selectedIndex >= options.length
      ) {
        throw new Error(`${control.type}.selectedIndex is out of range.`);
      }
      break;
    }
    case "textField":
      requireString(control.label, "textField.label");
      if (typeof control.value !== "string") {
        throw new Error("textField.value must be a string.");
      }
      if (control.placeholder !== undefined && typeof control.placeholder !== "string") {
        throw new Error("textField.placeholder must be a string.");
      }
      if (control.autocomplete !== undefined && typeof control.autocomplete !== "string") {
        throw new Error("textField.autocomplete must be a string.");
      }
      break;
    case "textArea":
      requireString(control.label, "textArea.label");
      if (typeof control.value !== "string") {
        throw new Error("textArea.value must be a string.");
      }
      break;
    case "checkbox":
      requireString(control.label, "checkbox.label");
      if (typeof control.checked !== "boolean") {
        throw new Error("checkbox.checked must be a boolean.");
      }
      break;
    case "slider":
      requireString(control.label, "slider.label");
      if (
        !isInt32(control.min) ||
        !isInt32(control.max) ||
        !isInt32(control.value) ||
        control.min >= control.max ||
        control.value < control.min ||
        control.value > control.max
      ) {
        throw new Error("slider requires an integer value within its range.");
      }
      break;
    case "scrollPanel":
      requireString(control.label, "scrollPanel.label");
      validateDimension(control.height, "scrollPanel.height");
      if (!Array.isArray(control.children)) {
        throw new Error("scrollPanel.children must be an array.");
      }
      control.children.forEach((id, index) =>
        requireString(id, `scrollPanel.children[${index}]`),
      );
      break;
    case "panel":
      if (!Array.isArray(control.children)) {
        throw new Error("panel.children must be an array.");
      }
      control.children.forEach((id, index) =>
        requireString(id, `panel.children[${index}]`),
      );
      break;
    case "group":
      if (!Array.isArray(control.children)) {
        throw new Error("group.children must be an array.");
      }
      control.children.forEach((id, index) =>
        requireString(id, `group.children[${index}]`),
      );
      validateLayout(control.layout ?? { type: "vertical" }, ["vertical", "horizontal"], "group.layout");
      break;
    default:
      throw new Error(`Unsupported control type: ${control.type}`);
  }
}

export function validateLayout(layout, types, name) {
  if (layout === null || typeof layout !== "object" || Array.isArray(layout) ||
      !types.includes(layout.type)) {
    throw new Error(`${name}.type must be one of: ${types.join(", ")}.`);
  }
  if (layout.media === undefined) return;
  if (layout.type === "center") {
    throw new Error(`${name}.media is not supported for a center layout.`);
  }
  if (!Array.isArray(layout.media)) {
    throw new Error(`${name}.media must be an array.`);
  }
  layout.media.forEach((rule, index) => {
    const ruleName = `${name}.media[${index}]`;
    if (rule === null || typeof rule !== "object" || Array.isArray(rule) ||
        !["vertical", "horizontal"].includes(rule.type)) {
      throw new Error(`${ruleName}.type must be 'vertical' or 'horizontal'.`);
    }
    for (const key of ["minWidth", "maxWidth"]) {
      if (rule[key] !== undefined && (!isInt32(rule[key]) || rule[key] < 0)) {
        throw new Error(`${ruleName}.${key} must be a non-negative integer.`);
      }
    }
    if (rule.minWidth === undefined && rule.maxWidth === undefined) {
      throw new Error(`${ruleName} needs minWidth or maxWidth.`);
    }
  });
}

export function resolveLayoutType(layout, width) {
  let type = layout.type;
  for (const rule of layout.media ?? []) {
    if ((rule.minWidth === undefined || width >= rule.minWidth) &&
        (rule.maxWidth === undefined || width <= rule.maxWidth)) {
      type = rule.type;
    }
  }
  return type;
}

function validateControlMedia(media) {
  if (!Array.isArray(media)) {
    throw new Error("control.media must be an array.");
  }
  for (const [index, rule] of media.entries()) {
    const name = `control.media[${index}]`;
    if (rule === null || typeof rule !== "object" || Array.isArray(rule)) {
      throw new Error(`${name} must be an object.`);
    }
    const hasMin = rule.minWidth !== undefined;
    const hasMax = rule.maxWidth !== undefined;
    if (!hasMin && !hasMax) {
      throw new Error(`${name} needs minWidth or maxWidth.`);
    }
    for (const key of ["minWidth", "maxWidth"]) {
      if (rule[key] !== undefined && (!isInt32(rule[key]) || rule[key] < 0)) {
        throw new Error(`${name}.${key} must be a non-negative integer.`);
      }
    }
    if (typeof rule.visible !== "boolean") {
      throw new Error(`${name}.visible must be a boolean.`);
    }
  }
}

export function resolveControlVisibility(media, width) {
  let visible = true;
  for (const rule of media ?? []) {
    if ((rule.minWidth === undefined || width >= rule.minWidth) &&
        (rule.maxWidth === undefined || width <= rule.maxWidth)) {
      visible = rule.visible;
    }
  }
  return visible;
}

function validateActions(control) {
  if (control.actions === undefined) {
    return;
  }
  if (
    control.actions === null ||
    typeof control.actions !== "object" ||
    Array.isArray(control.actions)
  ) {
    throw new Error("control.actions must be an object.");
  }

  const eventTypes = {
    button: ["click"],
    comboBox: ["change"],
    textField: ["input", "enter"],
    textArea: ["input"],
    checkbox: ["change"],
    radioGroup: ["change"],
    slider: ["input"],
    list: ["change"],
  };
  for (const [eventType, action] of Object.entries(control.actions)) {
    if (!eventTypes[control.type]?.includes(eventType)) {
      throw new Error(`${control.type} does not support the '${eventType}' action event.`);
    }
    if (action === null || typeof action !== "object" || Array.isArray(action)) {
      throw new Error(`${eventType} action must specify a file and function.`);
    }
    const file = requireString(action.file, `${eventType}.action.file`);
    if (
      !file.endsWith(".js") ||
      file.startsWith("/") ||
      /^[a-z]:/i.test(file) ||
      file.replaceAll("\\", "/").split("/").includes("..")
    ) {
      throw new Error("Action files must be relative .js paths without parent segments.");
    }
    const functionName = requireString(action.function, `${eventType}.action.function`);
    if (!/^[A-Za-z_$][\w$]*$/.test(functionName)) {
      throw new Error("Action function must be a JavaScript identifier.");
    }
  }
}

function validateControlTree(order, controls) {
  const visited = new Set();
  const visiting = new Set();

  function visit(id) {
    requireString(id, "control id");
    if (!Object.hasOwn(controls, id)) {
      throw new Error(`Unknown control id: ${id}`);
    }
    if (visiting.has(id)) {
      throw new Error(`Control hierarchy contains a cycle at: ${id}`);
    }
    if (visited.has(id)) {
      throw new Error(`Control id is referenced more than once: ${id}`);
    }

    visiting.add(id);
    const control = controls[id];
    validateControl(control);
    if (["scrollPanel", "panel", "group"].includes(control.type)) {
      control.children.forEach(visit);
    }
    visiting.delete(id);
    visited.add(id);
  }

  order.forEach(visit);
  for (const id of Object.keys(controls)) {
    if (!visited.has(id) && controls[id].type === "panel") {
      visit(id);
    }
  }
  if (visited.size !== Object.keys(controls).length) {
    throw new Error("Every control must be reachable from window.layout.children.");
  }
}

function validateMenus(menus) {
  if (!Array.isArray(menus)) {
    throw new Error("menus must be an array.");
  }
  menus.forEach((menu) => {
    requireString(menu.label, "menu.label");
    if (!Array.isArray(menu.items) || menu.items.length === 0) {
      throw new Error("menu.items must contain at least one item.");
    }
    menu.items.forEach((item) => {
      requireString(item.text, "menu item.text");
      requireString(item.message, "menu item.message");
    });
  });
}

export function validateUi(ui) {
  const layout = ui?.window?.layout;
  if (
    ui === null ||
    typeof ui !== "object" ||
    Array.isArray(ui) ||
    layout === null ||
    typeof layout !== "object" ||
    !Array.isArray(layout.children) ||
    typeof ui.window?.title !== "string" ||
    !isInt32(ui.window.width) ||
    ui.window.width <= 0 ||
    !isInt32(ui.window.height) ||
    ui.window.height <= 0 ||
    ui.controls === null ||
    typeof ui.controls !== "object" ||
    Array.isArray(ui.controls)
  ) {
    throw new Error("The UI definition has missing or invalid values.");
  }
  if (ui.window.fullscreen !== undefined && typeof ui.window.fullscreen !== "boolean") {
    throw new Error("window.fullscreen must be a boolean.");
  }
  validateOptionalColor(ui.window.backgroundColor, "window.backgroundColor");
  validateOptionalColor(ui.window.foregroundColor, "window.foregroundColor");
  if (ui.theme !== undefined) {
    if (ui.theme === null || typeof ui.theme !== "object" || Array.isArray(ui.theme)) {
      throw new Error("theme must be an object.");
    }
    validateOptionalColor(ui.theme.backgroundColor, "theme.backgroundColor");
    validateOptionalColor(ui.theme.foregroundColor, "theme.foregroundColor");
  }
  validateLayout(layout, ["vertical", "horizontal", "center"], "window.layout");
  validateControlTree(layout.children, ui.controls);
  validateMenus(ui.menus);
  return ui;
}

export function loadConfiguration(store, resourcePath, includeStack = new Set()) {
  const normalizedPath = normalizeResourcePath(resourcePath);
  if (includeStack.has(normalizedPath)) {
    throw new Error(`Configuration include cycle at: ${normalizedPath}`);
  }
  const nextStack = new Set(includeStack);
  nextStack.add(normalizedPath);

  const config = JSON.parse(store.readText(normalizedPath));
  if (config === null || typeof config !== "object" || Array.isArray(config)) {
    throw new Error(`Configuration ${normalizedPath} must contain a JSON object.`);
  }

  let merged = {};
  const scripts = [];
  if (config.includes !== undefined) {
    if (!Array.isArray(config.includes)) {
      throw new Error("includes must be an array.");
    }
    for (const include of config.includes) {
      requireString(include, "includes[]");
      const included = loadConfiguration(
        store,
        resolveResourcePath(normalizedPath, include),
        nextStack,
      );
      appendScripts(scripts, included.scripts);
      merged = mergeObjects(merged, included);
    }
  }
  merged = mergeObjects(merged, config);
  appendScripts(scripts, readScripts(config.scripts, normalizedPath));
  merged.scripts = scripts;
  return merged;
}

function readScripts(value, basePath) {
  if (value === undefined) return [];
  if (!Array.isArray(value)) {
    throw new Error("scripts must be an array.");
  }
  return value.map((item) => {
    if (typeof item !== "string" || item.trim() === "") {
      throw new Error("scripts must contain non-empty paths.");
    }
    const resolved = resolveResourcePath(basePath, item);
    if (!resolved.endsWith(".js")) {
      throw new Error("Script files must be relative .js paths without parent segments.");
    }
    return resolved;
  });
}

function appendScripts(target, more) {
  for (const path of more ?? []) {
    if (!target.includes(path)) target.push(path);
  }
}

function isMergeObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function mergeObjects(target, source) {
  const merged = { ...target };
  for (const [key, value] of Object.entries(source)) {
    if (key === "includes" || key === "scripts") continue;
    merged[key] = isMergeObject(merged[key]) && isMergeObject(value)
      ? mergeObjects(merged[key], value)
      : value;
  }
  return merged;
}
