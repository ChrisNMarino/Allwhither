plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val allwhitherProject = providers.gradleProperty("allwhitherProject").orElse("example").get()
val repoDir = rootProject.layout.projectDirectory.dir("../..")
val pythonExe = if (System.getProperty("os.name").startsWith("Windows")) "python" else "python3"
val projectEntry = repoDir.file("projects/$allwhitherProject/$allwhitherProject.json").asFile
require(allwhitherProject.matches(Regex("^[A-Za-z0-9][A-Za-z0-9._-]*$"))) {
    "allwhitherProject must be a project directory name."
}
require(projectEntry.isFile) {
    "Missing ${projectEntry.path}."
}
val generateEmbeddedResources = tasks.register<Exec>("generateEmbeddedResources") {
    val embedScript = repoDir.file("tools/embed_resources.py")
    val projectDirectory = repoDir.dir("projects/$allwhitherProject")
    val generatedFile = layout.buildDirectory.file(
        "generated/allwhitherEmbedded/org/allwhither/starter/EmbeddedResourceData.kt")
    inputs.file(embedScript)
    inputs.dir(projectDirectory)
    outputs.file(generatedFile)
    doFirst {
        generatedFile.get().asFile.parentFile.mkdirs()
        layout.buildDirectory.dir("generated/allwhitherAssets").get().asFile.deleteRecursively()
    }
    commandLine(
        pythonExe,
        embedScript.asFile.absolutePath,
        "--platform", "kotlin",
        "--project-dir", projectDirectory.asFile.absolutePath,
        "--output", generatedFile.get().asFile.absolutePath
    )
}

val generateAppName = tasks.register<Exec>("generateAppName") {
    val embedScript = repoDir.file("tools/embed_resources.py")
    val projectDirectory = repoDir.dir("projects/$allwhitherProject")
    val generatedFile = layout.buildDirectory.file(
        "generated/allwhitherRes/values/strings.xml")
    inputs.file(embedScript)
    inputs.dir(projectDirectory)
    outputs.file(generatedFile)
    doFirst {
        generatedFile.get().asFile.parentFile.mkdirs()
    }
    commandLine(
        pythonExe,
        embedScript.asFile.absolutePath,
        "--platform", "android",
        "--project-dir", projectDirectory.asFile.absolutePath,
        "--output", generatedFile.get().asFile.absolutePath
    )
}

android {
    namespace = "org.allwhither.starter"
    compileSdk = 35

    kotlinOptions {
        jvmTarget = "1.8"
    }

    buildFeatures {
        buildConfig = true
    }

    defaultConfig {
        applicationId = "org.allwhither.starter"
        minSdk = 23
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
        buildConfigField("String", "ALLWHITHER_PROJECT", "\"$allwhitherProject\"")
    }

    sourceSets {
        getByName("main") {
            java.srcDir(layout.buildDirectory.dir("generated/allwhitherEmbedded"))
            res.srcDir(layout.buildDirectory.dir("generated/allwhitherRes"))
        }
    }

}

tasks.named("preBuild").configure {
    dependsOn(generateEmbeddedResources, generateAppName)
}

tasks.configureEach {
    if (name != "generateEmbeddedResources" &&
        name != "generateAppName" &&
        (name.contains("Kotlin") || name.contains("JavaCompile"))
    ) {
        dependsOn(generateEmbeddedResources, generateAppName)
    }
}

android.applicationVariants.configureEach {
    val projectOutputName = "$allwhitherProject-${name}.apk"
    outputs.configureEach {
        (this as com.android.build.gradle.internal.api.BaseVariantOutputImpl).outputFileName =
            projectOutputName
    }
}

dependencies {
    implementation("org.mozilla:rhino:1.7.15")
}
