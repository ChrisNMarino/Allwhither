
function onEntrySelected(dom) {
  var next = dom.getSelectedIndex("entry-list");
  if (next === vaultFile.index) {
    return;
  }
  commitEntry(dom);
  vaultFile.index = next;
  if (next < 0 || next >= vaultFile.entries.length) {
    clearEntry(dom);
    return;
  }
  showEntry(dom);
}

function newEntry(dom) {
  commitEntry(dom);
  vaultFile.entries.push({
    title: "New entry",
    username: "",
    password: "",
    notes: ""
  });
  vaultFile.index = vaultFile.entries.length - 1;
  vaultFile.dirty = true;
  refreshEntries(dom);
  writeVault(dom);
}

function saveEntry(dom) {
  if (vaultFile.index < 0 || vaultFile.index >= vaultFile.entries.length) {
    dom.setStatus("Choose New to add an entry.");
    return;
  }
  if (commitEntry(dom)) {
    refreshEntries(dom);
  }
  writeVault(dom);
}

function deleteEntry(dom) {
  if (vaultFile.index < 0 || vaultFile.index >= vaultFile.entries.length) {
    dom.setStatus("There is no entry to delete.");
    return;
  }
  vaultFile.entries.splice(vaultFile.index, 1);
  if (vaultFile.index >= vaultFile.entries.length) {
    vaultFile.index = vaultFile.entries.length - 1;
  }
  vaultFile.dirty = true;
  refreshEntries(dom);
  writeVault(dom);
}

function saveVault(dom) {
  commitEntry(dom);
  refreshEntries(dom);
  writeVault(dom);
}

function lockVault(dom) {
  var discarded = vaultFile.dirty;
  vaultFile.password = "";
  vaultFile.entries = [];
  vaultFile.index = -1;
  vaultFile.dirty = false;
  showFilePanel(dom);
  dom.setStatus(discarded
    ? "Vault locked. Unsaved changes were discarded."
    : "Vault locked.");
}
