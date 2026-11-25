
var currentPanel = "file-panel";

function showFilePanel(dom) {
  showPanel(dom, "file-panel");
}

function showPasswordPanel(dom, mode) {
  showPanel(dom, "auth-panel");
  dom.setValue("master-password", "");
  dom.setText(
    "password-file",
    mode === "create"
      ? "Create a password for the new vault."
      : "Enter the password for " + vaultFile.name + "."
  );
  passwordMode = mode;
}

function showPanel(dom, id) {
  if (currentPanel === id) {
    return;
  }
  dom.removePanel(currentPanel);
  dom.addPanel(id);
  currentPanel = id;
}

var passwordMode = "open";

function showEntries(dom) {
  showPanel(dom, "entries-panel");
  dom.setText("vault-caption", vaultFile.name || "Vault");
  refreshEntries(dom);
}

function refreshEntries(dom) {
  var selectors = ["entry-list-compact", "entry-list-wide"];
  var titles = vaultFile.entries.map(function (entry) {
    return entry.title || "Untitled";
  });
  if (vaultFile.index >= titles.length) {
    vaultFile.index = titles.length - 1;
  }
  if (vaultFile.index < -1) {
    vaultFile.index = -1;
  }
  var options = ["Add New"].concat(titles);
  selectors.forEach(function (controlId) {
    dom.setOptions(controlId, options, vaultFile.index + 1);
  });
  if (vaultFile.index < 0) {
    clearEntry(dom);
  } else {
    showEntry(dom);
  }
  updateEntryActions(dom);
}

function updateEntryActions(dom) {
  dom.setVisible("delete-entry", vaultFile.index >= 0);
}