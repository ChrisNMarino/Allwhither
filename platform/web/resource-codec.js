// Must match KEY in tools/embed_resources.py. XOR obfuscation, not encryption.
export const RESOURCE_KEY = Uint8Array.of(
  0x6f, 0xa3, 0x19, 0xc4, 0x57, 0xe8, 0x2b, 0x91,
  0xd0, 0x44, 0x7e, 0x13, 0xb6, 0x08, 0xf5, 0x6a,
);

export function obfuscateBytes(input) {
  const bytes = input instanceof Uint8Array ? input : Uint8Array.from(input);
  const output = new Uint8Array(bytes.length);
  for (let index = 0; index < bytes.length; index += 1) {
    const mask = RESOURCE_KEY[index % RESOURCE_KEY.length] ^ ((index * 13 + 7) & 0xff);
    output[index] = bytes[index] ^ mask;
  }
  return output;
}

export function bytesToBase64(bytes) {
  if (typeof Buffer !== "undefined") {
    return Buffer.from(bytes).toString("base64");
  }
  let binary = "";
  for (let index = 0; index < bytes.length; index += 1) {
    binary += String.fromCharCode(bytes[index]);
  }
  return btoa(binary);
}

export function base64ToBytes(value) {
  if (typeof Buffer !== "undefined") {
    return new Uint8Array(Buffer.from(value, "base64"));
  }
  const binary = atob(value);
  const bytes = new Uint8Array(binary.length);
  for (let index = 0; index < binary.length; index += 1) {
    bytes[index] = binary.charCodeAt(index);
  }
  return bytes;
}

export function normalizeResourcePath(relativePath) {
  if (typeof relativePath !== "string" || relativePath.length === 0) {
    throw new Error("Resource path must be a non-empty relative path.");
  }
  const slashPath = relativePath.replaceAll("\\", "/");
  if (slashPath.startsWith("/") || /^[a-z]:/i.test(slashPath)) {
    throw new Error(`Resource path '${relativePath}' must stay inside the project.`);
  }
  const parts = [];
  for (const segment of slashPath.split("/")) {
    if (segment === "" || segment === ".") continue;
    if (segment === "..") {
      if (parts.length === 0) {
        throw new Error(`Resource path '${relativePath}' must stay inside the project.`);
      }
      parts.pop();
      continue;
    }
    parts.push(segment);
  }
  if (parts.length === 0) {
    throw new Error("Resource path must be a non-empty relative path.");
  }
  return parts.join("/");
}

export function resolveResourcePath(currentResource, relativePath) {
  const normalizedCurrent = normalizeResourcePath(currentResource);
  const slash = normalizedCurrent.lastIndexOf("/");
  const parent = slash === -1 ? "" : normalizedCurrent.slice(0, slash);
  const combined = parent === "" ? relativePath : `${parent}/${relativePath}`;
  return normalizeResourcePath(combined);
}

export class ResourceStore {
  constructor(files) {
    this.files = files;
  }

  readText(relativePath) {
    const normalized = normalizeResourcePath(relativePath);
    const encoded = this.files?.[normalized];
    if (typeof encoded !== "string") {
      throw new Error(`Embedded resource '${normalized}' was not found.`);
    }
    return new TextDecoder().decode(obfuscateBytes(base64ToBytes(encoded)));
  }
}
