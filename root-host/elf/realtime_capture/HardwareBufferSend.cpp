#include <android/hardware_buffer.h>
#include <android/hardware_buffer_jni.h>
#include <jni.h>

#include <cerrno>

// HardwareBuffer remains owned by the Java caller during this synchronous call.
// The NDK sender duplicates its native-handle FDs into the receiving process;
// it does not copy pixels or transfer ownership of the Java HardwareBuffer.
extern "C" JNIEXPORT jint JNICALL
Java_com_aurora_capture_CaptureHelper_nativeSend(JNIEnv* env, jclass,
                                               jobject hardwareBuffer,
                                               jobject socketDescriptor) {
    if (hardwareBuffer == nullptr || socketDescriptor == nullptr) {
        return -EINVAL;
    }

    jclass descriptorClass = env->GetObjectClass(socketDescriptor);
    if (descriptorClass == nullptr) {
        return -EINVAL;
    }
    // Android's java.io.FileDescriptor stores the Linux FD in "descriptor".
    jfieldID descriptorField = env->GetFieldID(descriptorClass, "descriptor", "I");
    env->DeleteLocalRef(descriptorClass);
    if (descriptorField == nullptr) {
        return -EINVAL;
    }
    const int socketFd = env->GetIntField(socketDescriptor, descriptorField);
    if (socketFd < 0) {
        return -EBADF;
    }

    AHardwareBuffer* buffer = AHardwareBuffer_fromHardwareBuffer(env, hardwareBuffer);
    if (buffer == nullptr) {
        return -EINVAL;
    }
    return AHardwareBuffer_sendHandleToUnixSocket(buffer, socketFd);
}
