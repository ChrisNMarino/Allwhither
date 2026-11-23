using Jint;
using Jint.Native;

namespace Allwhither.Windows;

public sealed class NativeApi
{
    public HttpClientApi http { get; }
    public FileApi file { get; }
    public CryptoApi crypto { get; }

    public NativeApi(Engine engine)
    {
        http = new HttpClientApi(engine);
        file = new FileApi(engine);
        crypto = new CryptoApi(engine);
    }

    public CryptoThenable encrypt(JsValue plaintext, JsValue password) => crypto.encrypt(plaintext, password);
    public CryptoThenable decrypt(JsValue ciphertext, JsValue password) => crypto.decrypt(ciphertext, password);

    public string platform() => "windows";
}
