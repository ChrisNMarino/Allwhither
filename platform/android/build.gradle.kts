plugins {
    id("com.android.application") version "8.7.3" apply false
    id("org.jetbrains.kotlin.android") version "2.0.21" apply false
}

val omniProject = providers.gradleProperty("omniProject").orElse("example").get()
require(omniProject.matches(Regex("^[A-Za-z0-9][A-Za-z0-9._-]*$"))) {
    "omniProject must be a project directory name."
}
val omniBuildDir = rootProject.layout.projectDirectory.dir("../../build/$omniProject/android")
rootProject.layout.buildDirectory.set(omniBuildDir)
subprojects {
    project.layout.buildDirectory.set(omniBuildDir.dir(project.name))
}
