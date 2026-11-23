plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val omniProject = providers.gradleProperty("omniProject").orElse("example").get()
val repoDir = rootProject.layout.projectDirectory.dir("../..")
val projectEntry = repoDir.file("projects/$omniProject/$omniProject.omni").asFile
require(omniProject.matches(Regex("^[A-Za-z0-9][A-Za-z0-9._-]*$"))) {
    "omniProject must be a project directory name."
}
require(projectEntry.isFile) {
    "Missing ${projectEntry.path}."
}
val generateEmbeddedResources = tasks.register<Exec>("generateEmbeddedResources") {
    val embedScript = repoDir.file("tools/embed_resources.py")
    val projectDirectory = repoDir.dir("projects/$omniProject")
    val generatedFile = layout.buildDirectory.file(
        "generated/omniEmbedded/org/omni/starter/EmbeddedResourceData.kt")
    inputs.file(embedScript)
    inputs.dir(projectDirectory)
    outputs.file(generatedFile)
    doFirst {
        generatedFile.get().asFile.parentFile.mkdirs()
        layout.buildDirectory.dir("generated/omniAssets").get().asFile.deleteRecursively()
    }
    commandLine(
        "python3",
        embedScript.asFile.absolutePath,
        "--platform", "kotlin",
        "--project-dir", projectDirectory.asFile.absolutePath,
        "--output", generatedFile.get().asFile.absolutePath
    )
}

val generateAppName = tasks.register<Exec>("generateAppName") {
    val embedScript = repoDir.file("tools/embed_resources.py")
    val projectDirectory = repoDir.dir("projects/$omniProject")
    val generatedFile = layout.buildDirectory.file(
        "generated/omniRes/values/strings.xml")
    inputs.file(embedScript)
    inputs.dir(projectDirectory)
    outputs.file(generatedFile)
    doFirst {
        generatedFile.get().asFile.parentFile.mkdirs()
    }
    commandLine(
        "python3",
        embedScript.asFile.absolutePath,
        "--platform", "android",
        "--project-dir", projectDirectory.asFile.absolutePath,
        "--output", generatedFile.get().asFile.absolutePath
    )
}

android {
    namespace = "org.omni.starter"
    compileSdk = 35

    kotlinOptions {
        jvmTarget = "1.8"
    }

    buildFeatures {
        buildConfig = true
    }

    defaultConfig {
        applicationId = "org.omni.starter"
        minSdk = 23
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
        buildConfigField("String", "OMNI_PROJECT", "\"$omniProject\"")
    }

    sourceSets {
        getByName("main") {
            java.srcDir(layout.buildDirectory.dir("generated/omniEmbedded"))
            res.srcDir(layout.buildDirectory.dir("generated/omniRes"))
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
    val projectOutputName = "$omniProject-${name}.apk"
    outputs.configureEach {
        (this as com.android.build.gradle.internal.api.BaseVariantOutputImpl).outputFileName =
            projectOutputName
    }
}

dependencies {
    implementation("org.mozilla:rhino:1.7.15")
}
