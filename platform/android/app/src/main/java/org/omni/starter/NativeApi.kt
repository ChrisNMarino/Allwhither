package org.omni.starter

import org.mozilla.javascript.Scriptable

class NativeApi(activity: MainActivity) {
    val http = HttpClientApi()
    val file = FileApi(activity)
    val crypto = CryptoApi()

    fun attach(scope: Scriptable) {
        http.attach(scope)
        file.attach(scope)
        crypto.attach(scope)
    }

    fun encrypt(plaintext: Any?, password: Any?): Any = crypto.encrypt(plaintext, password)
    fun decrypt(ciphertext: Any?, password: Any?): Any = crypto.decrypt(ciphertext, password)

    fun platform(): String = "android"
}
