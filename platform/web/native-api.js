import { HttpClient } from "./http-client.js";
import { FileApi } from "./file-api.js";
import { CryptoApi } from "./crypto-api.js";

export class NativeApi {
  constructor() {
    const cryptoInstance = new CryptoApi();
    Object.defineProperty(this, "http", {
      value: new HttpClient(),
      writable: false,
      enumerable: true,
    });
    Object.defineProperty(this, "file", {
      value: new FileApi(),
      writable: false,
      enumerable: true,
    });
    Object.defineProperty(this, "crypto", {
      value: cryptoInstance,
      writable: false,
      enumerable: true,
    });
    this.encrypt = (plaintext, password) => cryptoInstance.encrypt(plaintext, password);
    this.decrypt = (ciphertext, password) => cryptoInstance.decrypt(ciphertext, password);
  }

  platform() {
    return "web";
  }
}
