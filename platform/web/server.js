import http from "node:http";
import fs from "node:fs/promises";
import path from "node:path";

const contentRoot = process.argv[2] ? path.resolve(process.argv[2]) : "";
const contentTypes = {
  ".css": "text/css; charset=utf-8",
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".json": "application/json; charset=utf-8"
};
const host = process.env.HOST || "127.0.0.1";
const port = Number(process.env.PORT || 8080);

if (process.argv.length !== 3 || contentRoot === "") {
  console.error("Usage: node platform/web/server.js <build-directory>");
  process.exit(1);
}

if (!Number.isInteger(port) || port < 1 || port > 65535) {
  console.error("PORT must be an integer between 1 and 65535.");
  process.exit(1);
}

try {
  await fs.access(path.join(contentRoot, "payload.json"));
} catch {
  console.error(`No payload.json in ${contentRoot}.`);
  console.error("Build the web app with ./build.sh web <project>.");
  process.exit(1);
}

function resolveRequestPath(pathname) {
  if (pathname === "/" || pathname === "/web" || pathname === "/web/") {
    return path.join(contentRoot, "index.html");
  }

  if (!pathname.startsWith("/web/")) {
    return null;
  }

  const filePath = path.resolve(contentRoot, pathname.slice("/web/".length));
  const relativePath = path.relative(contentRoot, filePath);
  if (
    relativePath === ".." ||
    relativePath.startsWith(`..${path.sep}`) ||
    path.isAbsolute(relativePath)
  ) {
    return null;
  }

  return filePath;
}

const server = http.createServer(async (request, response) => {
  if (request.method !== "GET" && request.method !== "HEAD") {
    response.writeHead(405, { Allow: "GET, HEAD" });
    response.end();
    return;
  }

  let pathname;
  try {
    pathname = decodeURIComponent(new URL(request.url, "http://localhost").pathname);
  } catch {
    response.writeHead(400);
    response.end();
    return;
  }

  const filePath = resolveRequestPath(pathname);
  if (filePath === null) {
    response.writeHead(404);
    response.end();
    return;
  }

  try {
    const contents = await fs.readFile(filePath);
    response.writeHead(200, {
      "Content-Type": contentTypes[path.extname(filePath)] || "application/octet-stream",
      "Cache-Control": "no-store",
      "Content-Length": contents.length
    });
    response.end(request.method === "HEAD" ? undefined : contents);
  } catch (error) {
    if (error.code === "ENOENT" || error.code === "EISDIR") {
      response.writeHead(404);
      response.end();
      return;
    }

    console.error(`Could not serve ${pathname}:`, error);
    response.writeHead(500);
    response.end();
  }
});

server.listen(port, host, () => {
  console.log(`omni web starter: http://${host}:${port}/`);
  console.log(`Serving ${contentRoot}`);
});
