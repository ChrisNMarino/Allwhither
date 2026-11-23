using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Jint;
using Jint.Native;
using Jint.Runtime;

namespace Allwhither.Windows;

public sealed class CryptoApi
{
    private readonly Engine engine;

    public CryptoApi(Engine engine)
    {
        this.engine = engine;
    }

    public CryptoThenable encrypt(JsValue plaintextValue, JsValue passwordValue)
    {
        SynchronizationContext? context = SynchronizationContext.Current;
        if (!plaintextValue.IsString() || !passwordValue.IsString())
        {
            return CryptoThenable.Failed(engine, "Plaintext and password must be strings.", context);
        }

        string plaintext = plaintextValue.AsString();
        string password = passwordValue.AsString();
        if (string.IsNullOrEmpty(password))
        {
            return CryptoThenable.Failed(engine, "Password must not be empty.", context);
        }

        var task = Task.Run(() =>
        {
            byte[] salt = new byte[16];
            byte[] iv = new byte[12];
            RandomNumberGenerator.Fill(salt);
            RandomNumberGenerator.Fill(iv);

            using var kdf = new Rfc2898DeriveBytes(password, salt, 10000, HashAlgorithmName.SHA256);
            byte[] key = kdf.GetBytes(32);

            byte[] plaintextBytes = Encoding.UTF8.GetBytes(plaintext);
            byte[] ciphertextBytes = new byte[plaintextBytes.Length];
            byte[] tagBytes = new byte[16];

            using var aesGcm = new AesGcm(key, 16);
            aesGcm.Encrypt(iv, plaintextBytes, ciphertextBytes, tagBytes);

            var payload = new Dictionary<string, object>
            {
                ["version"] = 1,
                ["salt"] = Convert.ToBase64String(salt),
                ["iv"] = Convert.ToBase64String(iv),
                ["tag"] = Convert.ToBase64String(tagBytes),
                ["data"] = Convert.ToBase64String(ciphertextBytes)
            };

            return (JsValue)new JsString(JsonSerializer.Serialize(payload));
        });

        return CryptoThenable.FromTask(engine, task, context);
    }

    public CryptoThenable decrypt(JsValue ciphertextValue, JsValue passwordValue)
    {
        SynchronizationContext? context = SynchronizationContext.Current;
        if (!ciphertextValue.IsString() || !passwordValue.IsString())
        {
            return CryptoThenable.Failed(engine, "Ciphertext and password must be strings.", context);
        }

        string ciphertextJson = ciphertextValue.AsString();
        string password = passwordValue.AsString();
        if (string.IsNullOrEmpty(password))
        {
            return CryptoThenable.Failed(engine, "Password must not be empty.", context);
        }

        var task = Task.Run(() =>
        {
            using var doc = JsonDocument.Parse(ciphertextJson);
            JsonElement root = doc.RootElement;
            byte[] salt = Convert.FromBase64String(root.GetProperty("salt").GetString()!);
            byte[] iv = Convert.FromBase64String(root.GetProperty("iv").GetString()!);
            byte[] tag = Convert.FromBase64String(root.GetProperty("tag").GetString()!);
            byte[] data = Convert.FromBase64String(root.GetProperty("data").GetString()!);

            using var kdf = new Rfc2898DeriveBytes(password, salt, 10000, HashAlgorithmName.SHA256);
            byte[] key = kdf.GetBytes(32);

            byte[] decryptedBytes = new byte[data.Length];
            using var aesGcm = new AesGcm(key, 16);
            try
            {
                aesGcm.Decrypt(iv, data, tag, decryptedBytes);
            }
            catch (CryptographicException)
            {
                throw new InvalidOperationException("Decryption failed: invalid password or corrupted data.");
            }

            return (JsValue)new JsString(Encoding.UTF8.GetString(decryptedBytes));
        });

        return CryptoThenable.FromTask(engine, task, context);
    }
}

public sealed class CryptoThenable
{
    private readonly Engine engine;
    private readonly Task<JsValue> task;
    private readonly SynchronizationContext? context;

    private CryptoThenable(Engine engine, Task<JsValue> task, SynchronizationContext? context)
    {
        this.engine = engine;
        this.task = task;
        this.context = context;
    }

    public static CryptoThenable FromTask(Engine engine, Task<JsValue> task, SynchronizationContext? context) =>
        new(engine, task, context);

    public static CryptoThenable Failed(Engine engine, string message, SynchronizationContext? context) =>
        new(engine, Task.FromException<JsValue>(new InvalidOperationException(message)), context);

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
                        string error = completed.Exception?.GetBaseException().Message ?? "Crypto operation failed.";
                        Invoke(onRejected, error);
                        return;
                    }
                    Invoke(onFulfilled, completed.Result);
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
}
