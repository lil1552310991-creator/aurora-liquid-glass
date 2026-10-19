plugins {
    id("com.android.application")
}

android {
    namespace = "com.aurora.runtime"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.aurora.runtime"
        minSdk = 24
        targetSdk = 36
        ndkVersion = "27.0.12077973"
        versionCode = 1
        versionName = "0.1.0"

        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++20", "-Wall", "-Wextra")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

dependencies {
}
