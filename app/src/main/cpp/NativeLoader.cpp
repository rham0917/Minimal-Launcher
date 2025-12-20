#include <jni.h>
#include <android/native_activity.h>
#include <android/log.h>
#include <dlfcn.h>
#include <mutex>

#define LOG_TAG "NativeLoader"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static std::mutex gMutex;
static void* gHandle = nullptr;

static void (*gOnCreate)(ANativeActivity*, void*, size_t) = nullptr;
static void (*gOnFinish)(ANativeActivity*) = nullptr;
static void (*gAndroidMain)(struct android_app*) = nullptr;

extern "C" {

JNIEXPORT void ANativeActivity_onCreate(
        ANativeActivity* activity,
        void* savedState,
        size_t savedStateSize) {
    if (gOnCreate) {
        gOnCreate(activity, savedState, savedStateSize);
    } else {
        LOGE("ANativeActivity_onCreate called before library loaded");
    }
}

JNIEXPORT void ANativeActivity_finish(ANativeActivity* activity) {
    if (gOnFinish) {
        gOnFinish(activity);
    }
}

JNIEXPORT void android_main(struct android_app* state) {
    if (gAndroidMain) {
        gAndroidMain(state);
    } else {
        LOGE("android_main called before library loaded");
    }
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    if (!vm) {
        LOGE("JNI_OnLoad called with null VM");
        return JNI_ERR;
    }
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL
Java_io_kitsuri_pelauncher_Launcher_MinecraftActivity_nativeOnLauncherLoaded(
        JNIEnv* env,
        jobject /* thiz */,
        jstring libPath) {

    if (!env || !libPath) {
        LOGE("nativeOnLauncherLoaded: invalid arguments");
        return;
    }

    std::lock_guard<std::mutex> lock(gMutex);

    if (gHandle) {
        LOGD("Native library already loaded, skipping");
        return;
    }

    const char* path = env->GetStringUTFChars(libPath, nullptr);
    if (!path) {
        LOGE("Failed to get UTF chars for libPath");
        return;
    }

    LOGD("Loading native library: %s", path);

    gHandle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!gHandle) {
        LOGE("dlopen failed: %s", dlerror());
        env->ReleaseStringUTFChars(libPath, path);
        return;
    }

    dlerror();

    gOnCreate = reinterpret_cast<decltype(gOnCreate)>(
            dlsym(gHandle, "ANativeActivity_onCreate"));
    gOnFinish = reinterpret_cast<decltype(gOnFinish)>(
            dlsym(gHandle, "ANativeActivity_finish"));
    gAndroidMain = reinterpret_cast<decltype(gAndroidMain)>(
            dlsym(gHandle, "android_main"));

    const char* error = dlerror();
    if (error || !gOnCreate || !gAndroidMain) {
        LOGE("Symbol resolution failed: %s", error ? error : "unknown");

        dlclose(gHandle);
        gHandle = nullptr;
        gOnCreate = nullptr;
        gOnFinish = nullptr;
        gAndroidMain = nullptr;

        env->ReleaseStringUTFChars(libPath, path);
        return;
    }

    LOGD("Minecraft native library loaded successfully");

    env->ReleaseStringUTFChars(libPath, path);
}

} // extern "C"
