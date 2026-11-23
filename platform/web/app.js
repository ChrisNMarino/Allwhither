import { loadConfiguration, validateUi } from "./ui-config.js";
import { chromeColors } from "./colors.js";
import { renderControl, renderMenus } from "./control-renderer.js";
import { ActionRuntime } from "./action-runtime.js";
import { ResourceStore } from "./resource-codec.js";
import { UiDom } from "./ui-dom.js";

async function startApplication() {
  const response = await fetch("/web/payload.json");
  if (!response.ok) {
    throw new Error(`Could not load the web payload: ${response.status}`);
  }
  const payload = await response.json();
  if (typeof payload.entry !== "string" || payload.entry === "" ||
      payload.files === null || typeof payload.files !== "object") {
    throw new Error("The web payload is missing its Omni files.");
  }
  const resources = new ResourceStore(payload.files);
  const application = loadConfiguration(resources, payload.entry);
  const ui = validateUi(application.ui);
  const status = document.getElementById("status");
  const dom = new UiDom(ui.controls, status);
  const actionRuntime = new ActionRuntime(
    ui.controls,
    dom,
    resources,
    application.scripts ?? [],
  );
  await actionRuntime.prepare();
  const theme = ui.theme ?? {};
  const controls = document.getElementById("controls");
  dom.setMount(controls, (controlId) => renderControl(
    controlId, ui.controls, status, actionRuntime, dom, theme,
  ));

  document.title = ui.window.title;
  document.getElementById("title").textContent = ui.window.title;
  const windowElement = document.getElementById("window");
  const chrome = chromeColors(ui.window);
  if (chrome.background) {
    windowElement.style.backgroundColor = chrome.background;
    document.body.style.backgroundColor = chrome.background;
    windowElement.style.setProperty("--omni-chrome-bg", chrome.background);
  }
  if (chrome.foreground) {
    windowElement.style.color = chrome.foreground;
    status.style.color = chrome.foreground;
    windowElement.style.setProperty("--omni-chrome-fg", chrome.foreground);
  }
  if (chrome.background || chrome.foreground) {
    windowElement.classList.add("themed-chrome");
  }
  const fullscreenCenter = ui.window.fullscreen === true &&
    ui.window.layout.type === "center";
  windowElement.classList.toggle("fullscreen", ui.window.fullscreen === true);

  document.getElementById("title").hidden = fullscreenCenter;
  document.getElementById("menus").hidden = fullscreenCenter;
  status.hidden = fullscreenCenter;
  renderMenus(ui.menus, status, document.getElementById("menus"));
  controls.classList.toggle("centered", ui.window.layout.type === "center");
  ui.window.layout.children.forEach((controlId) => {
    controls.append(renderControl(
      controlId, ui.controls, status, actionRuntime, dom, theme,
    ));
  });
}

startApplication().catch((error) => {
  console.error(error);
  document.body.textContent = error.message;
});
