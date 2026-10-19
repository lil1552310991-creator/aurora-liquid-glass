#include "Renderer.h"

#include <android/native_window_jni.h>
#include <jni.h>

namespace {
aurora::Renderer& renderer() {
    static aurora::Renderer instance;
    return instance;
}
}

extern "C" JNIEXPORT void JNICALL
Java_com_aurora_runtime_AuroraSurfaceView_nativeSetHostMode(JNIEnv*, jclass, jint mode) {
    renderer().setHostMode(static_cast<aurora::HostMode>(mode));
}

extern "C" JNIEXPORT void JNICALL
Java_com_aurora_runtime_AuroraSurfaceView_nativeSetBackendKind(JNIEnv*, jclass, jint kind) {
    renderer().setBackendKind(kind == 1 ? aurora::BackendKind::Vulkan : aurora::BackendKind::Gles3);
}

extern "C" JNIEXPORT void JNICALL
Java_com_aurora_runtime_AuroraSurfaceView_nativeSetInitialTab(JNIEnv*, jclass, jint tab) {
    renderer().setInitialTab(tab);
}

extern "C" JNIEXPORT void JNICALL
Java_com_aurora_runtime_AuroraSurfaceView_nativeSetLiquidGlassVisible(JNIEnv*, jclass, jboolean visible) {
    renderer().setLiquidGlassVisible(visible == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_aurora_runtime_AuroraSurfaceView_nativeSurfaceCreated(
    JNIEnv* env, jclass, jobject surface) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    renderer().surfaceCreated(window);
    if (window != nullptr) {
        ANativeWindow_release(window);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_aurora_runtime_AuroraSurfaceView_nativeSurfaceChanged(
    JNIEnv*, jclass, jint width, jint height, jfloat density) {
    renderer().surfaceChanged(width, height, density);
}

extern "C" JNIEXPORT void JNICALL
Java_com_aurora_runtime_AuroraSurfaceView_nativeSafeRegion(
    JNIEnv*, jclass, jfloat left, jfloat top, jfloat right, jfloat bottom) {
    renderer().setSafeRegion(left, top, right, bottom);
}

extern "C" JNIEXPORT void JNICALL
Java_com_aurora_runtime_AuroraSurfaceView_nativeSurfaceDestroyed(JNIEnv*, jclass) {
    renderer().surfaceDestroyed();
}

extern "C" JNIEXPORT void JNICALL
Java_com_aurora_runtime_AuroraSurfaceView_nativeTouch(
    JNIEnv*, jclass, jint action, jint pointerId, jfloat x, jfloat y, jfloat pressure,
    jfloat size, jint toolType, jdouble timeSeconds) {
    renderer().touch(action, pointerId, x, y, pressure, size, toolType, timeSeconds);
}

extern "C" JNIEXPORT void JNICALL
Java_com_aurora_runtime_AuroraTouchProxyView_nativeTouch(
    JNIEnv*, jclass, jint action, jint pointerId, jfloat x, jfloat y, jfloat pressure,
    jfloat size, jint toolType, jdouble timeSeconds) {
    renderer().touch(action, pointerId, x, y, pressure, size, toolType, timeSeconds);
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_aurora_runtime_AuroraTouchProxyView_nativeGetInteractiveRegion(JNIEnv* env, jclass) {
    const aurora::TouchRegion region = renderer().interactiveRegion();
    jfloat values[] = {region.left, region.top, region.right, region.bottom};
    jfloatArray result = env->NewFloatArray(4);
    if (result != nullptr) {
        env->SetFloatArrayRegion(result, 0, 4, values);
    }
    return result;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_aurora_runtime_AuroraTouchProxyView_nativeWantsTextInput(JNIEnv*, jclass) {
    return renderer().wantsTextInput() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_aurora_runtime_AuroraTouchProxyView_nativeText(JNIEnv* env, jclass, jstring text) {
    const char* utf8 = text == nullptr ? nullptr : env->GetStringUTFChars(text, nullptr);
    renderer().textInput(utf8);
    if (text != nullptr && utf8 != nullptr) env->ReleaseStringUTFChars(text, utf8);
}

extern "C" JNIEXPORT void JNICALL
Java_com_aurora_runtime_AuroraTouchProxyView_nativeDeleteBackward(JNIEnv*, jclass) {
    renderer().deleteBackward();
}


extern "C" JNIEXPORT void JNICALL
Java_com_aurora_runtime_AuroraSurfaceView_nativeText(
    JNIEnv* env, jclass, jstring text) {
    const char* utf8 = text == nullptr ? nullptr : env->GetStringUTFChars(text, nullptr);
    renderer().textInput(utf8);
    if (text != nullptr && utf8 != nullptr) {
        env->ReleaseStringUTFChars(text, utf8);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_aurora_runtime_AuroraSurfaceView_nativeDeleteBackward(JNIEnv*, jclass) {
    renderer().deleteBackward();
}

extern "C" JNIEXPORT void JNICALL
Java_com_aurora_runtime_AuroraProjectionService_nativeProjectionFrame(
    JNIEnv* env, jclass, jbyteArray rgba, jint width, jint height) {
    if (rgba == nullptr || width <= 0 || height <= 0 || width > 4096 || height > 4096) return;
    const jsize size = env->GetArrayLength(rgba);
    const jsize expected = width * height * 4;
    if (size != expected) return;
    jbyte* data = env->GetByteArrayElements(rgba, nullptr);
    if (data == nullptr) return;
    renderer().projectionFrame(reinterpret_cast<const std::uint8_t*>(data),
                               static_cast<std::size_t>(size), width, height);
    env->ReleaseByteArrayElements(rgba, data, JNI_ABORT);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_aurora_runtime_AuroraSurfaceView_nativeWantsTextInput(JNIEnv*, jclass) {
    return renderer().wantsTextInput() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_aurora_runtime_AuroraSurfaceView_nativePause(JNIEnv*, jclass) {
    renderer().pause();
}

extern "C" JNIEXPORT void JNICALL
Java_com_aurora_runtime_AuroraSurfaceView_nativeResume(JNIEnv*, jclass) {
    renderer().resume();
}
