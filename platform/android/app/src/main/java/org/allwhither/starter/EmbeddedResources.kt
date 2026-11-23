package org.allwhither.starter

import java.io.FileNotFoundException
import java.nio.charset.StandardCharsets

internal object EmbeddedResources {
    fun readText(relativePath: String): String {
        val normalized = normalize(relativePath)
        val data = EmbeddedResourceData.files[normalized]
            ?: throw FileNotFoundException("Embedded resource '$normalized' was not found.")
        return decode(data)
    }

    fun resolve(currentResource: String, relativePath: String): String {
        val normalizedCurrent = normalize(currentResource)
        val slash = normalizedCurrent.lastIndexOf('/')
        val parent = if (slash == -1) "" else normalizedCurrent.substring(0, slash)
        val combined = if (parent.isEmpty()) relativePath else "$parent/$relativePath"
        return normalize(combined)
    }

    fun normalize(relativePath: String): String {
        if (relativePath.isEmpty()) {
            throw IllegalArgumentException("Resource path must be a non-empty relative path.")
        }
        val slashPath = relativePath.replace('\\', '/')
        if (slashPath[0] == '/' ||
            (slashPath.length >= 2 && slashPath[0].isLetter() && slashPath[1] == ':')
        ) {
            throw IllegalArgumentException(
                "Resource path '$relativePath' must stay inside the project.")
        }
        val parts = mutableListOf<String>()
        for (segment in slashPath.split('/')) {
            if (segment.isEmpty() || segment == ".") continue
            if (segment == "..") {
                if (parts.isEmpty()) {
                    throw IllegalArgumentException(
                        "Resource path '$relativePath' must stay inside the project.")
                }
                parts.removeAt(parts.lastIndex)
                continue
            }
            parts.add(segment)
        }
        if (parts.isEmpty()) {
            throw IllegalArgumentException("Resource path must be a non-empty relative path.")
        }
        return parts.joinToString("/")
    }

    private fun decode(data: ByteArray): String {
        val key = EmbeddedResourceData.key
        val plain = ByteArray(data.size)
        for (index in data.indices) {
            val mask = (key[index % key.size].toInt() and 0xFF) xor ((index * 13 + 7) and 0xFF)
            plain[index] = ((data[index].toInt() and 0xFF) xor mask).toByte()
        }
        return String(plain, StandardCharsets.UTF_8)
    }
}
