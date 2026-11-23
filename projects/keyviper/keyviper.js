
var currentPanel = "file-panel";

function showFilePanel(dom) {
  showPanel(dom, "file-panel");
  dom.setValue("master-password", "");
  dom.setText(
    "chosen-file",
    vaultFile.ciphertext ? "Selected: " + vaultFile.name : "No file selected"
  );
}

function showPanel(dom, id) {
  if (currentPanel === id) {
    return;
  }
  dom.removePanel(currentPanel);
  dom.addPanel(id);
  currentPanel = id;
}

function showEntries(dom) {
  showPanel(dom, "entries-panel");
  dom.setText("vault-caption", vaultFile.name || "Vault");
  refreshEntries(dom);
}