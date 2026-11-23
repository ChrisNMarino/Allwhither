package org.allwhither.starter

import android.os.Handler
import android.os.Looper
import android.util.Base64
import android.util.Log
import org.json.JSONObject
import org.mozilla.javascript.Context
import org.mozilla.javascript.Function
import org.mozilla.javascript.Scriptable
import java.nio.charset.StandardCharsets
import java.security.SecureRandom
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import javax.crypto.Cipher
import javax.crypto.SecretKeyFactory
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.PBEKeySpec
import javax.crypto.spec.SecretKeySpec

class CryptoApi {
    private val mainHandler = Handler(Looper.getMainLooper())
    private lateinit var scope: Scriptable

    fun attach(scope: Scriptable) {
        this.scope = scope
    }

    fun encrypt(plaintext: Any?, password: Any?): Any {
        val pending = CryptoThenable()
        val plainStr = plaintext as? String
        val passStr = password as? String
        if (plainStr == null || passStr.isNullOrEmpty()) {
            pending.reject("Plaintext and a non-empty password string are required.")
            return pending
        }

        executor.execute {
            try {
                val random = SecureRandom()
                val salt = ByteArray(16)
                val iv = ByteArray(12)
                random.nextBytes(salt)
                random.nextBytes(iv)

                val spec = PBEKeySpec(passStr.toCharArray(), salt, 10000, 256)
                val factory = SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256")
                val key = SecretKeySpec(factory.generateSecret(spec).encoded, "AES")

                val cipher = Cipher.getInstance("AES/GCM/NoPadding")
                cipher.init(Cipher.ENCRYPT_MODE, key, GCMParameterSpec(128, iv))
                val fullCipher = cipher.doFinal(plainStr.toByteArray(StandardCharsets.UTF_8))

                val tag = fullCipher.copyOfRange(fullCipher.size - 16, fullCipher.size)
                val data = fullCipher.copyOfRange(0, fullCipher.size - 16)

                val payload = JSONObject().apply {
                    put("version", 1)
                    put("salt", Base64.encodeToString(salt, Base64.NO_WRAP))
                    put("iv", Base64.encodeToString(iv, Base64.NO_WRAP))
                    put("tag", Base64.encodeToString(tag, Base64.NO_WRAP))
                    put("data", Base64.encodeToString(data, Base64.NO_WRAP))
                }
                pending.resolve(payload.toString())
            } catch (e: Exception) {
                pending.reject(e.message ?: "Encryption failed.")
            }
        }
        return pending
    }

    fun decrypt(ciphertext: Any?, password: Any?): Any {
        val pending = CryptoThenable()
        val cipherStr = ciphertext as? String
        val passStr = password as? String
        if (cipherStr.isNullOrEmpty() || passStr.isNullOrEmpty()) {
            pending.reject("Ciphertext and a non-empty password string are required.")
            return pending
        }

        executor.execute {
            try {
                val json = JSONObject(cipherStr)
                val salt = Base64.decode(json.getString("salt"), Base64.NO_WRAP)
                val iv = Base64.decode(json.getString("iv"), Base64.NO_WRAP)
                val tag = Base64.decode(json.getString("tag"), Base64.NO_WRAP)
                val data = Base64.decode(json.getString("data"), Base64.NO_WRAP)

                val spec = PBEKeySpec(passStr.toCharArray(), salt, 10000, 256)
                val factory = SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256")
                val key = SecretKeySpec(factory.generateSecret(spec).encoded, "AES")

                val cipher = Cipher.getInstance("AES/GCM/NoPadding")
                cipher.init(Cipher.DECRYPT_MODE, key, GCMParameterSpec(128, iv))

                val combined = ByteArray(data.size + tag.size)
                System.arraycopy(data, 0, combined, 0, data.size)
                System.arraycopy(tag, 0, combined, data.size, tag.size)

                val decrypted = cipher.doFinal(combined)
                pending.resolve(String(decrypted, StandardCharsets.UTF_8))
            } catch (e: Exception) {
                pending.reject("Decryption failed: invalid password or corrupted data.")
            }
        }
        return pending
    }

    inner class CryptoThenable {
        private val lock = Any()
        private var settled = false
        private var success: String? = null
        private var failure: String? = null
        private val waiters = mutableListOf<Pair<Function?, Function?>>()

        fun resolve(value: String) {
            settle(value, null)
        }

        fun reject(message: String) {
            settle(null, message)
        }

        fun then(onFulfilled: Any?) {
            arm(onFulfilled, null)
        }

        fun then(onFulfilled: Any?, onRejected: Any?) {
            arm(onFulfilled, onRejected)
        }

        private fun settle(result: String?, message: String?) {
            val pending = synchronized(lock) {
                if (settled) return
                settled = true
                success = result
                failure = message
                val copy = waiters.toList()
                waiters.clear()
                copy
            }
            if (pending.isEmpty()) return
            mainHandler.post {
                for (waiter in pending) {
                    deliver(waiter.first, waiter.second)
                }
            }
        }

        private fun arm(onFulfilled: Any?, onRejected: Any?) {
            val fulfilled = onFulfilled as? Function
            val rejected = onRejected as? Function
            val deliverNow = synchronized(lock) {
                if (!settled) {
                    waiters.add(fulfilled to rejected)
                    false
                } else {
                    true
                }
            }
            if (deliverNow) {
                mainHandler.post { deliver(fulfilled, rejected) }
            }
        }

        private fun deliver(onFulfilled: Function?, onRejected: Function?) {
            val context = Context.enter()
            try {
                context.optimizationLevel = -1
                val message = failure
                if (message != null) {
                    onRejected?.call(context, scope, scope, arrayOf(message))
                    return
                }
                val result = success ?: return
                onFulfilled?.call(context, scope, scope, arrayOf(result))
            } catch (exception: Exception) {
                Log.w("allwhither", "JavaScript Crypto callback failed", exception)
            } finally {
                Context.exit()
            }
        }
    }

    companion object {
        private val executor: ExecutorService = Executors.newCachedThreadPool { runnable ->
            Thread(runnable, "allwhither-crypto").apply { isDaemon = true }
        }
    }
}
