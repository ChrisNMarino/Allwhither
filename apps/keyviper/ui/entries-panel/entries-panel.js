
function onEntrySelectedCompact(dom) {
  onEntrySelected(dom, "entry-list-compact");
}

function onEntrySelectedWide(dom) {
  onEntrySelected(dom, "entry-list-wide");
}

function onEntrySelected(dom, controlId) {
  var next = Math.max(-1, dom.getSelectedIndex(controlId) - 1);
  if (next === vaultFile.index) {
    return;
  }
  var changed = commitEntry(dom);
  vaultFile.index = next;
  refreshEntries(dom);
  if (changed) {
    writeVault(dom);
  }
}

function saveEntry(dom) {
  var changed;
  if (vaultFile.index < 0) {
    vaultFile.entries.push(entryFromForm(dom));
    vaultFile.index = vaultFile.entries.length - 1;
    vaultFile.dirty = true;
    changed = true;
  } else if (vaultFile.index >= vaultFile.entries.length) {
    dom.setStatus("Select an entry before saving.");
    return;
  } else {
    changed = commitEntry(dom);
  }
  refreshEntries(dom);
  if (changed) {
    writeVault(dom);
  }
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
