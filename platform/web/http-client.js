const DEFAULT_TIMEOUT_MS = 30000;

function reject(message) {
  return Promise.reject(message);
}

function isHttpUrl(url) {
  try {
    const parsed = new URL(url);
    return parsed.protocol === "http:" || parsed.protocol === "https:";
  } catch {
    return false;
  }
}

function readOptions(options) {
  if (options == null) {
    return { headers: {}, body: undefined, timeout: DEFAULT_TIMEOUT_MS };
  }
  if (typeof options !== "object" || Array.isArray(options)) {
    throw new Error("HTTP options must be an object.");
  }
  const timeout = options.timeout == null ? DEFAULT_TIMEOUT_MS : Number(options.timeout);
  if (!Number.isFinite(timeout) || timeout <= 0) {
    throw new Error("HTTP timeout must be a positive number of milliseconds.");
  }
  const headers = {};
  if (options.headers != null) {
    if (typeof options.headers !== "object" || Array.isArray(options.headers)) {
      throw new Error("HTTP headers must be an object.");
    }
    for (const [name, value] of Object.entries(options.headers)) {
      if (value !== null && typeof value === "object") {
        throw new Error("HTTP headers must be an object.");
      }
      headers[String(name)] = value == null ? "" : String(value);
    }
  }
  let body;
  if (options.body != null) {
    if (typeof options.body !== "string") {
      throw new Error("HTTP body must be a string.");
    }
    body = options.body;
  }
  return { headers, body, timeout };
}

function perform(method, url, options) {
  const normalized = String(method ?? "").toUpperCase();
  if (!/^[A-Z][A-Z0-9]*$/.test(normalized)) {
    return reject("Invalid HTTP method.");
  }
  if (typeof url !== "string" || url === "") {
    return reject("A URL is required.");
  }
  let parsed;
  try {
    parsed = new URL(url);
  } catch {
    return reject("Invalid URL.");
  }
  if (parsed.protocol !== "http:" && parsed.protocol !== "https:") {
    return reject("Only http and https URLs are supported.");
  }
  if (!isHttpUrl(url)) {
    return reject("Only http and https URLs are supported.");
  }
  let requestOptions;
  try {
    requestOptions = readOptions(options);
  } catch (error) {
    return reject(error.message);
  }
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), requestOptions.timeout);
  const hasBody = requestOptions.body != null &&
    normalized !== "GET" &&
    normalized !== "HEAD";
  return fetch(parsed.href, {
    method: normalized,
    headers: requestOptions.headers,
    body: hasBody ? requestOptions.body : undefined,
    redirect: "follow",
    signal: controller.signal,
  }).then(async (response) => {
    const headers = {};
    response.headers.forEach((value, key) => {
      headers[key] = value;
    });
    return {
      ok: response.ok,
      status: response.status,
      statusText: response.statusText,
      headers,
      body: await response.text(),
      url: response.url,
    };
  }).catch((error) => {
    if (error?.name === "AbortError") {
      throw "Request timed out.";
    }
    throw error?.message || String(error);
  }).finally(() => {
    clearTimeout(timer);
  });
}

export class HttpClient {
  get(url, options) {
    return perform("GET", url, options);
  }

  post(url, options) {
    return perform("POST", url, options);
  }

  put(url, options) {
    return perform("PUT", url, options);
  }

  patch(url, options) {
    return perform("PATCH", url, options);
  }

  delete(url, options) {
    return perform("DELETE", url, options);
  }

  head(url, options) {
    return perform("HEAD", url, options);
  }

  options(url, options) {
    return perform("OPTIONS", url, options);
  }

  request(method, url, options) {
    if (typeof method !== "string" || typeof url !== "string") {
      return reject("A method and URL are required.");
    }
    return perform(method, url, options);
  }
}
