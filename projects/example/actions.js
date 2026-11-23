
function sendHttpRequest(dom) {
  var method = dom.getValue("http-method");
  var url = dom.getValue("http-url");
  var options = { timeout: 15000 };
  if (method === "POST" || method === "PUT" || method === "PATCH") {
    options.headers = { "Content-Type": "application/json" };
    options.body = JSON.stringify({ source: "allwhither", method: method });
  }
  dom.setStatus("Sending " + method + " request...");
  dom.setValue("http-result", "");
  var pending;
  if (method === "GET") pending = api.http.get(url, options);
  else if (method === "POST") pending = api.http.post(url, options);
  else if (method === "PUT") pending = api.http.put(url, options);
  else if (method === "PATCH") pending = api.http.patch(url, options);
  else if (method === "DELETE") pending = api.http.delete(url, options);
  else if (method === "HEAD") pending = api.http.head(url, options);
  else if (method === "OPTIONS") pending = api.http.options(url, options);
  else pending = api.http.request(method, url, options);
  pending.then(function (response) {
    var body = response.body || "";
    if (body.length > 800) body = body.slice(0, 800) + "...";
    dom.setValue(
      "http-result",
      "HTTP " + response.status + " " + response.statusText + "\n" + body
    );
    dom.setStatus("HTTP " + response.status + " " + response.statusText);
  }, function (error) {
    dom.setValue("http-result", String(error));
    dom.setStatus("Request failed");
  });
}

function openFile(dom) {
  dom.setStatus("Opening file...");
  api.file.open({
    title: "Open Text File",
    filters: [
      { name: "Text Files", extensions: ["txt", "json", "js", "md"] }
    ]
  }).then(function (result) {
    if (result.canceled) {
      dom.setStatus("Open canceled");
      return;
    }
    dom.setValue("http-result", result.content || "");
    dom.setStatus("Opened: " + result.name);
  }, function (error) {
    dom.setStatus("Open failed: " + error);
  });
}

function saveFile(dom) {
  var content = dom.getValue("http-result") || "";
  dom.setStatus("Saving file...");
  api.file.save({
    name: "allwhither-output.txt",
    content: content,
    filters: [
      { name: "Text Files", extensions: ["txt", "json", "js", "md"] }
    ]
  }).then(function (result) {
    if (result.canceled) {
      dom.setStatus("Save canceled");
      return;
    }
    dom.setStatus("Saved: " + result.name);
  }, function (error) {
    dom.setStatus("Save failed: " + error);
  });
}
