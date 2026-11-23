var VAULT_FILTERS = [{ name: "Key Viper vault", extensions: ["keyviper"] }];

var vaultFile = emptyVault();

function emptyVault() {
  return {
    name: "",
    path: null,
    handle: null,
    ciphertext: "",
    password: "",
    entries: [],
    index: -1,
    dirty: false
  };
}

function chooseVault(dom) {
  dom.setStatus("Choose a vault file...");
  api.file.open({
    title: "Choose Key Viper vault",
    filters: VAULT_FILTERS
  }).then(function (result) {
    if (!result || result.canceled || result.ok === false) {
      dom.setStatus("Choose canceled");
      return;
    }
    vaultFile.name = result.name || "vault.keyviper";
    vaultFile.path = result.path || null;
    vaultFile.handle = result.handle || null;
    vaultFile.ciphertext = result.content || "";
    vaultFile.password = "";
    vaultFile.entries = [];
    vaultFile.index = -1;
    vaultFile.dirty = false;
    dom.setText("chosen-file", "Selected: " + vaultFile.name);
    dom.setStatus("File selected. Enter its password to open it.");
  }).catch(function (error) {
    dom.setStatus("Could not choose a file: " + error);
  });
}

function openVault(dom) {
  if (!vaultFile.ciphertext) {
    dom.setStatus("Choose a vault file first.");
    return;
  }
  var password = dom.getValue("master-password") || "";
  if (!password) {
    dom.setStatus("Enter the vault password.");
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

function createVault(dom) {
  var password = dom.getValue("master-password") || "";
  if (!password) {
    dom.setStatus("Enter a password for the new vault.");
    return;
  }
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

function refreshEntries(dom) {
  var titles = vaultFile.entries.map(function (entry) {
    return entry.title || "Untitled";
  });
  if (titles.length === 0) {
    vaultFile.index = -1;
    dom.setOptions("entry-list", []);
    clearEntry(dom);
    return;
  }
  if (vaultFile.index < 0 || vaultFile.index >= titles.length) {
    vaultFile.index = 0;
  }
  dom.setOptions("entry-list", titles, vaultFile.index);
  showEntry(dom);
}

function showEntry(dom) {
  var entry = vaultFile.entries[vaultFile.index];
  if (!entry) {
    clearEntry(dom);
    return;
  }
  dom.setValue("entry-title", entry.title);
  dom.setValue("entry-username", entry.username);
  dom.setValue("entry-password", entry.password);
  dom.setValue("entry-notes", entry.notes);
}

function clearEntry(dom) {
  dom.setValue("entry-title", "");
  dom.setValue("entry-username", "");
  dom.setValue("entry-password", "");
  dom.setValue("entry-notes", "");
}

function commitEntry(dom) {
  var entry = vaultFile.entries[vaultFile.index];
  if (!entry) {
    return false;
  }
  var next = {
    title: dom.getValue("entry-title") || "",
    username: dom.getValue("entry-username") || "",
    password: dom.getValue("entry-password") || "",
    notes: dom.getValue("entry-notes") || ""
  };
  if (entry.title === next.title &&
      entry.username === next.username &&
      entry.password === next.password &&
      entry.notes === next.notes) {
    return false;
  }
  vaultFile.entries[vaultFile.index] = next;
  vaultFile.dirty = true;
  return true;
}

function plaintextFor(vault) {
  return JSON.stringify({
    format: "keyviper",
    version: 1,
    entries: vault.entries
  });
}

function parseVault(plaintext) {
  var data;
  try {
    data = JSON.parse(plaintext);
  } catch (error) {
    throw new Error("This file is not a Key Viper vault.");
  }
  if (!data || data.format !== "keyviper" || data.version !== 1 ||
      !Array.isArray(data.entries)) {
    throw new Error("This file is not a Key Viper vault.");
  }
  return data.entries.map(function (entry) {
    if (!entry || typeof entry !== "object" || Array.isArray(entry)) {
      throw new Error("This file is not a Key Viper vault.");
    }
    return {
      title: entry.title == null ? "" : String(entry.title),
      username: entry.username == null ? "" : String(entry.username),
      password: entry.password == null ? "" : String(entry.password),
      notes: entry.notes == null ? "" : String(entry.notes)
    };
  });
}

function writeVault(dom) {
  if (!vaultFile.password) {
    dom.setStatus("The vault is locked.");
    return;
  }
  dom.setStatus("Saving vault...");
  api.crypto.encrypt(plaintextFor(vaultFile), vaultFile.password).then(function (encrypted) {
    return api.file.save({
      title: "Save Key Viper vault",
      name: vaultFile.name || "passwords.keyviper",
      content: encrypted,
      path: vaultFile.path,
      handle: vaultFile.handle,
      filters: VAULT_FILTERS
    }).then(function (result) {
      if (!result || result.canceled || result.ok === false) {
        dom.setStatus("Save canceled. Changes are still in memory.");
        return;
      }
      vaultFile.ciphertext = encrypted;
      if (result.path) vaultFile.path = result.path;
      if (result.handle) vaultFile.handle = result.handle;
      if (result.name) vaultFile.name = result.name;
      vaultFile.dirty = false;
      if (currentPanel === "entries-panel") {
        dom.setText("vault-caption", vaultFile.name);
      }
      dom.setStatus("Saved " + vaultFile.name);
    });
  }).catch(function (error) {
    dom.setStatus("Could not save the vault: " + error);
  });
}
