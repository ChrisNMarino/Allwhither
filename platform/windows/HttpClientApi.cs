using System.Net;
using System.Net.Http.Headers;
using System.Text;
using Jint;
using Jint.Native;
using Jint.Native.Array;
using Jint.Native.Object;
using Jint.Runtime;
using Jint.Runtime.Descriptors;

namespace Allwhither.Windows;

public sealed class HttpClientApi
{
    private const int DefaultTimeoutMs = 30000;

    private static readonly HttpClient Client = new(new HttpClientHandler
    {
        AllowAutoRedirect = true,
        MaxAutomaticRedirections = 20,
        AutomaticDecompression = DecompressionMethods.All
    })
    {
        Timeout = Timeout.InfiniteTimeSpan
    };

    private readonly Engine engine;

    public HttpClientApi(Engine engine)
    {
        this.engine = engine;
    }

    public HttpThenable get() => Send("GET", JsValue.Undefined, JsValue.Undefined);

    public HttpThenable get(JsValue url) => Send("GET", url, JsValue.Undefined);

    public HttpThenable get(JsValue url, JsValue options) => Send("GET", url, options);

    public HttpThenable post() => Send("POST", JsValue.Undefined, JsValue.Undefined);

    public HttpThenable post(JsValue url) => Send("POST", url, JsValue.Undefined);

    public HttpThenable post(JsValue url, JsValue options) => Send("POST", url, options);

    public HttpThenable put() => Send("PUT", JsValue.Undefined, JsValue.Undefined);

    public HttpThenable put(JsValue url) => Send("PUT", url, JsValue.Undefined);

    public HttpThenable put(JsValue url, JsValue options) => Send("PUT", url, options);

    public HttpThenable patch() => Send("PATCH", JsValue.Undefined, JsValue.Undefined);

    public HttpThenable patch(JsValue url) => Send("PATCH", url, JsValue.Undefined);

    public HttpThenable patch(JsValue url, JsValue options) => Send("PATCH", url, options);

    public HttpThenable delete() => Send("DELETE", JsValue.Undefined, JsValue.Undefined);

    public HttpThenable delete(JsValue url) => Send("DELETE", url, JsValue.Undefined);

    public HttpThenable delete(JsValue url, JsValue options) => Send("DELETE", url, options);

    public HttpThenable head() => Send("HEAD", JsValue.Undefined, JsValue.Undefined);

    public HttpThenable head(JsValue url) => Send("HEAD", url, JsValue.Undefined);

    public HttpThenable head(JsValue url, JsValue options) => Send("HEAD", url, options);

    public HttpThenable options() => Send("OPTIONS", JsValue.Undefined, JsValue.Undefined);

    public HttpThenable options(JsValue url) => Send("OPTIONS", url, JsValue.Undefined);

    public HttpThenable options(JsValue url, JsValue optionsValue) => Send("OPTIONS", url, optionsValue);

    public HttpThenable request() =>
        HttpThenable.Failed(engine, "A method and URL are required.", SynchronizationContext.Current);

    public HttpThenable request(JsValue method) =>
        HttpThenable.Failed(engine, "A method and URL are required.", SynchronizationContext.Current);

    public HttpThenable request(JsValue method, JsValue url) =>
        Request(method, url, JsValue.Undefined);

    public HttpThenable request(JsValue method, JsValue url, JsValue options) =>
        Request(method, url, options);

    private HttpThenable Request(JsValue method, JsValue url, JsValue options)
    {
        if (!method.IsString() || !url.IsString())
        {
            return HttpThenable.Failed(engine, "A method and URL are required.", SynchronizationContext.Current);
        }
        return Send(method.AsString().ToUpperInvariant(), url, options);
    }

    private HttpThenable Send(string method, JsValue urlValue, JsValue optionsValue)
    {
        SynchronizationContext? context = SynchronizationContext.Current;
        try
        {
            if (!urlValue.IsString() || string.IsNullOrEmpty(urlValue.AsString()))
            {
                return HttpThenable.Failed(engine, "A URL is required.", context);
            }
            string url = urlValue.AsString();
            if (!IsHttpUrl(url, out string? urlError))
            {
                return HttpThenable.Failed(engine, urlError!, context);
            }
            if (!IsMethod(method))
            {
                return HttpThenable.Failed(engine, "Invalid HTTP method.", context);
            }
            RequestOptions options = ReadOptions(optionsValue);
            return HttpThenable.Running(engine, SendAsync(method, url, options), context);
        }
        catch (Exception exception)
        {
            return HttpThenable.Failed(
                engine,
                string.IsNullOrEmpty(exception.Message) ? "Request failed." : exception.Message,
                context);
        }
    }

    private static async Task<HttpResult> SendAsync(string method, string url, RequestOptions options)
    {
        try
        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromMilliseconds(options.TimeoutMs));
            using var request = new HttpRequestMessage(new HttpMethod(method), url);
            ByteArrayContent? content = null;
            if (options.Body != null && method is not "GET" and not "HEAD")
            {
                content = new ByteArrayContent(Encoding.UTF8.GetBytes(options.Body));
                request.Content = content;
            }
            foreach (KeyValuePair<string, string> header in options.Headers)
            {
                if (content != null && content.Headers.TryAddWithoutValidation(header.Key, header.Value))
                {
                    continue;
                }
                request.Headers.TryAddWithoutValidation(header.Key, header.Value);
            }
            if (content != null && !HasHeader(options.Headers, "Content-Type"))
            {
                content.Headers.TryAddWithoutValidation("Content-Type", "text/plain;charset=UTF-8");
            }
            using HttpResponseMessage response = await Client.SendAsync(request, timeout.Token);
            string body = response.Content == null
                ? ""
                : await response.Content.ReadAsStringAsync(timeout.Token);
            return new HttpResult(
                response.IsSuccessStatusCode,
                (int) response.StatusCode,
                response.ReasonPhrase ?? "",
                ReadHeaders(response),
                body,
                response.RequestMessage?.RequestUri?.ToString() ?? url);
        }
        catch (OperationCanceledException)
        {
            throw new InvalidOperationException("Request timed out.");
        }
    }

    private static Dictionary<string, string> ReadHeaders(HttpResponseMessage response)
    {
        var headers = new Dictionary<string, string>(StringComparer.Ordinal);
        AddHeaders(headers, response.Headers);
        if (response.Content != null)
        {
            AddHeaders(headers, response.Content.Headers);
        }
        return headers;
    }

    private static void AddHeaders(Dictionary<string, string> target, HttpHeaders headers)
    {
        foreach (KeyValuePair<string, IEnumerable<string>> header in headers)
        {
            string name = header.Key.ToLowerInvariant();
            string value = string.Join(", ", header.Value);
            target[name] = target.TryGetValue(name, out string? existing)
                ? existing + ", " + value
                : value;
        }
    }

    private static RequestOptions ReadOptions(JsValue options)
    {
        if (options.IsUndefined() || options.IsNull())
        {
            return RequestOptions.Default();
        }
        if (!options.IsObject() || options.AsObject() is JsArray)
        {
            throw new ArgumentException("HTTP options must be an object.");
        }
        ObjectInstance source = options.AsObject();
        var result = RequestOptions.Default();
        JsValue timeout = source.Get("timeout");
        if (!timeout.IsUndefined() && !timeout.IsNull())
        {
            double milliseconds = timeout.IsNumber() ? timeout.AsNumber() : double.NaN;
            if (double.IsNaN(milliseconds) || double.IsInfinity(milliseconds) ||
                milliseconds <= 0 || milliseconds > int.MaxValue)
            {
                throw new ArgumentException("HTTP timeout must be a positive number of milliseconds.");
            }
            result.TimeoutMs = (int) milliseconds;
        }
        JsValue headers = source.Get("headers");
        if (!headers.IsUndefined() && !headers.IsNull())
        {
            if (!headers.IsObject() || headers.AsObject() is JsArray)
            {
                throw new ArgumentException("HTTP headers must be an object.");
            }
            ObjectInstance headerObject = headers.AsObject();
            foreach (KeyValuePair<JsValue, PropertyDescriptor> property in headerObject.GetOwnProperties())
            {
                if (!property.Key.IsString())
                {
                    continue;
                }
                JsValue value = headerObject.Get(property.Key);
                if (value.IsObject() && !value.IsNull())
                {
                    throw new ArgumentException("HTTP headers must be an object.");
                }
                result.Headers[property.Key.AsString()] = value.IsUndefined() || value.IsNull()
                    ? ""
                    : value.ToString();
            }
        }
        JsValue body = source.Get("body");
        if (!body.IsUndefined() && !body.IsNull())
        {
            if (!body.IsString())
            {
                throw new ArgumentException("HTTP body must be a string.");
            }
            result.Body = body.AsString();
        }
        return result;
    }

    private static bool IsHttpUrl(string url, out string? error)
    {
        if (!Uri.TryCreate(url, UriKind.Absolute, out Uri? uri) ||
            !uri.IsAbsoluteUri ||
            !url.StartsWith(uri.Scheme + ":", StringComparison.OrdinalIgnoreCase))
        {
            error = "Invalid URL.";
            return false;
        }
        if (uri.Scheme != Uri.UriSchemeHttp && uri.Scheme != Uri.UriSchemeHttps)
        {
            error = "Only http and https URLs are supported.";
            return false;
        }
        error = null;
        return true;
    }

    private static bool IsMethod(string method)
    {
        if (method.Length == 0 || !char.IsAsciiLetter(method[0]))
        {
            return false;
        }
        for (int index = 1; index < method.Length; index++)
        {
            if (!char.IsAsciiLetterOrDigit(method[index]))
            {
                return false;
            }
        }
        return true;
    }

    private static bool HasHeader(Dictionary<string, string> headers, string name)
    {
        foreach (string key in headers.Keys)
        {
            if (key.Equals(name, StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }
        return false;
    }

    private sealed class RequestOptions
    {
        public int TimeoutMs { get; set; } = DefaultTimeoutMs;

        public Dictionary<string, string> Headers { get; } = new(StringComparer.Ordinal);

        public string? Body { get; set; }

        public static RequestOptions Default() => new();
    }

    internal sealed record HttpResult(
        bool Ok,
        int Status,
        string StatusText,
        Dictionary<string, string> Headers,
        string Body,
        string Url);

    public sealed class HttpThenable
    {
        private readonly Engine engine;
        private readonly Task<HttpResult> task;
        private readonly SynchronizationContext? context;

        private HttpThenable(Engine engine, Task<HttpResult> task, SynchronizationContext? context)
        {
            this.engine = engine;
            this.task = task;
            this.context = context;
        }

        internal static HttpThenable Running(Engine engine, Task<HttpResult> task, SynchronizationContext? context) =>
            new(engine, task, context);

        public static HttpThenable Failed(Engine engine, string message, SynchronizationContext? context) =>
            new(engine, Task.FromException<HttpResult>(new InvalidOperationException(message)), context);

        public void then(JsValue onFulfilled) => Arm(onFulfilled, JsValue.Undefined);

        public void then(JsValue onFulfilled, JsValue onRejected) => Arm(onFulfilled, onRejected);

        private void Arm(JsValue onFulfilled, JsValue onRejected)
        {
            task.ContinueWith(completed =>
            {
                void Deliver()
                {
                    try
                    {
                        if (completed.IsFaulted || completed.IsCanceled)
                        {
                            Invoke(onRejected, ErrorText(completed));
                            return;
                        }
                        Invoke(onFulfilled, ToResponse(completed.Result));
                    }
                    catch (JavaScriptException)
                    {
                    }
                }

                if (context != null)
                {
                    context.Post(_ => Deliver(), null);
                }
                else
                {
                    Deliver();
                }
            }, TaskScheduler.Default);
        }

        private void Invoke(JsValue callback, object argument)
        {
            if (callback.IsUndefined() || callback.IsNull())
            {
                return;
            }
            engine.Invoke(callback, argument);
        }

        private JsValue ToResponse(HttpResult result)
        {
            var response = new JsObject(engine);
            response.Set("ok", result.Ok);
            response.Set("status", result.Status);
            response.Set("statusText", result.StatusText);
            response.Set("body", result.Body);
            response.Set("url", result.Url);
            var headers = new JsObject(engine);
            foreach (KeyValuePair<string, string> header in result.Headers)
            {
                headers.Set(header.Key, header.Value);
            }
            response.Set("headers", headers);
            return response;
        }

        private static string ErrorText(Task completed)
        {
            Exception? error = completed.Exception?.GetBaseException();
            if (error is InvalidOperationException && error.Message == "Request timed out.")
            {
                return error.Message;
            }
            return string.IsNullOrEmpty(error?.Message) ? "Request failed." : error.Message;
        }
    }
}
