# Allwhither

Allwhither is a small cross-platform UI starter: shared `.json` files use JSON to
describe an application and each platform builds its UI with native controls.

## Shared UI

Each project lives under `projects/` and uses
`projects/<project>/<project>.json` as its entry point. Platform implementations
live under `platform/`. The `example` project
includes `ui.json`. Included objects are merged in order, including nested
objects such as `ui.controls`. When both files set the same property, the
including file wins. Arrays are replaced rather than concatenated, except
`scripts`, which is concatenated. UI configuration lives under the top-level `ui` object. The main
window's `layout` lists the root control IDs in display order; nested panels
list child IDs the same way. Layout type `vertical` stacks controls,
`horizontal` places them side by side in equal-width columns, and `center`
centers them. A layout may add a `media` array of responsive rules, like CSS
media queries: `{ "minWidth": 720, "type": "horizontal" }`. `minWidth` and
`maxWidth` are inclusive window widths (pixels on web, Linux and Windows; dp on
Android), at least one is required, and when several rules match, the last one
wins. Rules only switch between `vertical` and `horizontal`, and `center`
layouts do not support `media`. The `example` project switches to two columns
at 720 wide. `window.width` and `window.height` set the Linux and Windows
window size. Android and web fill the screen or browser window. Set
`window.fullscreen` to `true` for a fullscreen window.
Supported controls are buttons, labels, combo boxes, text fields, checkboxes,
radio groups, sliders, selectable lists, and scrollable panels. Control values
and menu selections update a shared status message.
Controls are stored in the `controls` object, keyed by unique control IDs.
The window's `layout.children` lists root IDs in display order, and
`scrollPanel.children` lists nested control IDs. The `menus` collection is
rendered as the platform's menu affordance.
Any control may specify optional `backgroundColor` and `foregroundColor` values in `#RRGGBB` format. `window.backgroundColor` and `window.foregroundColor` color the window, title, menus, and status. An optional `ui.theme` object sets the default `backgroundColor` and `foregroundColor` for controls that do not set their own. A color on a control overrides the theme. When a background is set and no foreground is set, the framework uses black or white text, whichever has higher contrast on that background.
Each menu has `label` and `items`; each item has `text` and `message`, which is
shown in the status area when selected. The control fields are:

| Type | Fields |
| --- | --- |
| `label` | `text` |
| `button` | `text`, `clickedText` |
| `comboBox`, `radioGroup`, `list` | `label`, `options`, `selectedIndex` |
| `textField` | `label`, `value`, optional `placeholder`, optional `password`. A password field shows `hidden` in the status area instead of the typed text |
| `textArea` | `label`, `value` |
| `checkbox` | `label`, `checked` |
| `slider` | `label`, `min`, `max`, `value` |
| `scrollPanel` | `label`, `height`, `children` (control IDs) |
| `panel` | `label`, `children` (control IDs). A panel is a page. Panels may be left out of `window.layout.children` and mounted later |
| `group` | `children` (control IDs), optional `layout` (`type` `vertical` or `horizontal`, optional `media`; defaults to vertical). An unlabeled container for arranging controls |

Controls can define JavaScript handlers by event. Script paths are relative to
the application entry file's directory (the project directory here):

```json
"actions": {
  "click": { "file": "actions.js", "function": "handleClick" }
}
```

Handlers are global functions with the signature `function handler(dom, event)`.
Every platform evaluates action scripts in one shared JavaScript global.
JavaScript that no action names goes in the optional top-level `scripts` array.
Those files are evaluated in that same global before action scripts, and they
do not need to define a handler. Other scripts can call their functions and
read their variables. Each path is relative to the `.json` file that lists it.
Included files may list `scripts` as well. Those lists are joined in include
order, the including file's scripts follow, and a repeated path is evaluated
once.

```json
"scripts": ["keyviper.js"]
```

Scripts also have global `dom` and `api` objects. `dom` is access to UI
elements: `getText`/`setText`, `getValue`/`setValue`, `getChecked`/`setChecked`,
`getSecure`/`setSecure` (for password masking), and `setStatus`. `dom.addPanel(id)`
mounts a `panel` control that is not currently shown, and `dom.removePanel(id)`
removes that panel and its children. `dom.setOptions(id, options, selectedIndex)`
replaces the options of a list or combo box. `options` is an array of non-empty
strings and may be empty. `selectedIndex` is optional; it defaults to `0`, or to
no selection when the list is empty. `dom.getSelectedIndex(id)` returns the
selected list or combo box index, or `-1` when nothing is selected. `api` is the
host object for native code. `api.platform()` returns `linux`, `windows`,
`android`, or `web`. `api.http` sends requests with `get`, `post`, `put`,
`patch`, `delete`, `head`, `options`, and `request(method, url, options)`. Each
call returns a thenable. The options object is optional and accepts `headers`,
`body`, and `timeout` (milliseconds, default 30000). A resolved response is
`{ ok, status, statusText, headers, body, url }`. HTTP error statuses resolve;
transport and validation failures reject with a string. Only absolute `http`
and `https` URLs are accepted. `api.file` provides cross-platform file access.
Interactive methods show native file pickers: `api.file.open(options)` resolves to
`{ ok, canceled, name, content, path, handle }`, while `api.file.save(options)`
and `api.file.saveAs(options)` resolve to `{ ok, canceled, name, path, handle }`.
On the web, `api.file` uses the File System Access API with progressive fallback
to file upload and download elements. `api.file.storage` (also aliased on
`api.file`) provides sandboxed storage without user dialogs: `read(path)`,
`write(path, content)`, `delete(path)`, `exists(path)`, and `list(path)`.
Storage is scoped to application data on native platforms and to the Origin
Private File System (OPFS) with localStorage fallback on the web. `api.crypto`
(also aliased as `api.encrypt` and `api.decrypt`) provides standard AES-256-GCM
encryption with PBKDF2-HMAC-SHA256 key derivation across all platforms. The event includes
`type`, `targetId`, `text`, `value`, and `checked`. Supported events are
`click` on buttons, `change` on combo boxes, checkboxes, radio groups, and
lists, and `input` on text fields, text areas, and sliders.

## Projects

- **Linux / Wayland:** GTK 4 with JSON-GLib. GTK uses Wayland automatically in a
  Wayland session (and can also run under X11).
- **Windows:** .NET 8 Windows Forms.
- **Android:** Kotlin, Android SDK, and Gradle; controls use native Android UI.
- **Web:** HTML, CSS, and JavaScript. The build writes the project files into
  the web output as an obfuscated payload, which the page decodes in memory.

`projects/example/` contains the interactive controls demo.
`projects/helloworld/` contains a fullscreen, centered Hello World app.
`projects/keyviper/` contains Key Viper, a password manager. Its first panel
chooses or creates an encrypted vault file on the local file system. The vault
opens only with the correct password. After it opens, that panel is removed and
the entries panel lists the saved records.

## Run

A project is a directory under `projects/` whose entry file is
`projects/<project>/<project>.json`. Choose the platform and project from the
repository root. Nothing under `platform/` needs to change.

```sh
./build.sh linux example
./build.sh windows helloworld
./build.sh android example
./build.sh web helloworld
```

Omit either argument to be prompted. The prompt lists the projects it finds.
On Windows, use `build.bat` the same way, for example
`build.bat windows helloworld`.
Each build is written to `build/<project>/<platform>/`, so projects stay
separate. The web option serves the built directory at `http://127.0.0.1:8080/`.
Set `PORT` to use another port. Serve an existing web build again with
`node platform/web/server.js build/<project>/web`. Run `./clean.sh` or
`clean.bat` to remove generated build directories.

### Linux (Wayland)

Install GTK 4, JSON-GLib, JavaScriptCoreGTK, and libsoup development packages
(for example, `libgtk-4-dev`, `libjson-glib-dev`,
`libjavascriptcoregtk-4.1-dev`, and `libsoup-3.0-dev` on Debian/Ubuntu), then:

```sh
./build.sh linux example
./build/example/linux/example
```

### Windows

Install the .NET 8 SDK, then run `build.bat windows example`. The executable
is named after the project under `build/example/windows/`.

### Android

Install the Android SDK, a compatible JDK, and Gradle, then run
`./build.sh android example`. Install the resulting APK from
`build/example/android/` on a device or emulator.

### Web

Install Node.js 18 or newer, then run `./build.sh web example`. The page loads
the payload in `build/example/web/` and does not take a project argument.
