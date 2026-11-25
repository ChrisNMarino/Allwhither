
function backToFiles(dom) {
  dom.setValue("master-password", "");
  showFilePanel(dom);
}

function continueVault(dom) {
  var password = dom.getValue("master-password") || "";
  if (!password) {
    dom.setStatus("Enter the vault password.");
    return;
  }
  if (passwordMode === "create") {
    createVault(dom, password);
  } else {
    openVault(dom, password);
  }
}

function openVault(dom, password) {
  if (!vaultFile.ciphertext) {
    dom.setStatus("Choose a vault file first.");
    return;
  }
  dom.setStatus("Opening vault...");
  api.crypto.decrypt(vaultFile.ciphertext, password).then(function (plain) {
    var entries;
    try {
      entries = parseVault(plain);
    } catch (error) {
      dom.setStatus(error && error.message ? error.message : String(error));
      return;
    }
    vaultFile.password = password;
    vaultFile.entries = entries;
    vaultFile.index = entries.length ? 0 : -1;
    vaultFile.dirty = false;
    showEntries(dom);
    dom.setStatus(entries.length ? "Vault opened." : "Vault opened. It has no entries yet.");
  }).catch(function () {
    dom.setStatus("Could not open the vault. The password is incorrect or the file is damaged.");
  });
}

function createVault(dom, password) {
  var created = emptyVault();
  created.password = password;
  dom.setStatus("Choose where to save the new vault...");
  api.crypto.encrypt(plaintextFor(created), password).then(function (encrypted) {
    return api.file.saveAs({
      title: "Create Key Viper vault",
      name: "passwords.keyviper",
      content: encrypted,
      filters: VAULT_FILTERS
    }).then(function (result) {
      return { result: result, encrypted: encrypted };
    });
  }).then(function (saved) {
    if (!saved.result || saved.result.canceled || saved.result.ok === false) {
      dom.setStatus("Create canceled");
      return;
    }
    vaultFile = created;
    vaultFile.name = saved.result.name || "passwords.keyviper";
    vaultFile.path = saved.result.path || null;
    vaultFile.handle = saved.result.handle || null;
    vaultFile.ciphertext = saved.encrypted;
    showEntries(dom);
    dom.setStatus("Created " + vaultFile.name);
  }).catch(function (error) {
    dom.setStatus("Could not create the vault: " + error);
  });
}
