function toBase64(bytes) {
  let binary = "";
  const len = bytes.byteLength;
  for (let i = 0; i < len; i++) {
    binary += String.fromCharCode(bytes[i]);
  }
  return btoa(binary);
}

function fromBase64(base64) {
  const binary = atob(base64);
  const len = binary.length;
  const bytes = new Uint8Array(len);
  for (let i = 0; i < len; i++) {
    bytes[i] = binary.charCodeAt(i);
  }
  return bytes;
}

export class CryptoApi {
  async encrypt(plaintext, password) {
    if (typeof plaintext !== "string") {
      throw new Error("Plaintext must be a string.");
    }
    if (typeof password !== "string" || password.length === 0) {
      throw new Error("Password must be a non-empty string.");
    }

    const salt = crypto.getRandomValues(new Uint8Array(16));
    const iv = crypto.getRandomValues(new Uint8Array(12));

    const encoder = new TextEncoder();
    const keyMaterial = await crypto.subtle.importKey(
      "raw",
      encoder.encode(password),
      "PBKDF2",
      false,
      ["deriveKey"]
    );

    const key = await crypto.subtle.deriveKey(
      {
        name: "PBKDF2",
        salt,
        iterations: 10000,
        hash: "SHA-256",
      },
      keyMaterial,
      { name: "AES-GCM", length: 256 },
      false,
      ["encrypt", "decrypt"]
    );

    const ciphertextBuffer = await crypto.subtle.encrypt(
      {
        name: "AES-GCM",
        iv,
        tagLength: 128,
      },
      key,
      encoder.encode(plaintext)
    );

    const cipherBytes = new Uint8Array(ciphertextBuffer);
    const dataBytes = cipherBytes.subarray(0, cipherBytes.length - 16);
    const tagBytes = cipherBytes.subarray(cipherBytes.length - 16);

    return JSON.stringify({
      version: 1,
      salt: toBase64(salt),
      iv: toBase64(iv),
      tag: toBase64(tagBytes),
      data: toBase64(dataBytes),
    });
  }

  async decrypt(ciphertextJson, password) {
    if (typeof ciphertextJson !== "string" || ciphertextJson.trim() === "") {
      throw new Error("Ciphertext must be a valid encrypted JSON string.");
    }
    if (typeof password !== "string" || password.length === 0) {
      throw new Error("Password must be a non-empty string.");
    }

    let payload;
    try {
      payload = JSON.parse(ciphertextJson);
    } catch {
      throw new Error("Invalid encrypted data format.");
    }

    if (!payload.salt || !payload.iv || !payload.tag || !payload.data) {
      throw new Error("Malformed encrypted payload.");
    }

    const salt = fromBase64(payload.salt);
    const iv = fromBase64(payload.iv);
    const tag = fromBase64(payload.tag);
    const data = fromBase64(payload.data);

    // Combine data + tag for WebCrypto AES-GCM
    const combined = new Uint8Array(data.length + tag.length);
    combined.set(data, 0);
    combined.set(tag, data.length);

    const encoder = new TextEncoder();
    const keyMaterial = await crypto.subtle.importKey(
      "raw",
      encoder.encode(password),
      "PBKDF2",
      false,
      ["deriveKey"]
    );

    const key = await crypto.subtle.deriveKey(
      {
        name: "PBKDF2",
        salt,
        iterations: 10000,
        hash: "SHA-256",
      },
      keyMaterial,
      { name: "AES-GCM", length: 256 },
      false,
      ["encrypt", "decrypt"]
    );

    try {
      const decryptedBuffer = await crypto.subtle.decrypt(
        {
          name: "AES-GCM",
          iv,
          tagLength: 128,
        },
        key,
        combined
      );
      return new TextDecoder().decode(decryptedBuffer);
    } catch {
      throw new Error("Decryption failed: invalid password or corrupted data.");
    }
  }
}
