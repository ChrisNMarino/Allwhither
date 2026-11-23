plugins {
    id("com.android.application") version "8.7.3" apply false
    id("org.jetbrains.kotlin.android") version "2.0.21" apply false
}

val allwhitherProject = providers.gradleProperty("allwhitherProject").orElse("example").get()
require(allwhitherProject.matches(Regex("^[A-Za-z0-9][A-Za-z0-9._-]*$"))) {
    "allwhitherProject must be a project directory name."
}
val allwhitherBuildDir = rootProject.layout.projectDirectory.dir("../../build/$allwhitherProject/android")
rootProject.layout.buildDirectory.set(allwhitherBuildDir)
subprojects {
    project.layout.buildDirectory.set(allwhitherBuildDir.dir(project.name))
}
