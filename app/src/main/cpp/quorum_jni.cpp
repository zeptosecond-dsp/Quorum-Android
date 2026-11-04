// SPDX-License-Identifier: GPL-3.0-only
// The bridge: Kotlin (com.zeptosecond.quorum.Native) <-> the engine (engine/android/android_platform.c), and Oboe's
// output stream pulling the engine's sound.
#include <jni.h>
#include <android/log.h>
#include <oboe/Oboe.h>
#include <memory>
#include <mutex>

extern "C" {
void quorum_start(const char *dir);
void quorum_pull(float *out, int32_t n);
void quorum_set_latency(uint32_t frames);
uint32_t quorum_underruns(void);
void quorum_button(int32_t id, int32_t down);
void quorum_enc(int32_t id, int32_t detents);
void quorum_keys(uint32_t mask);
void quorum_midi(uint8_t st, uint8_t d1, uint8_t d2);
uint64_t quorum_leds(void);
uint64_t quorum_glow(void);
void quorum_copy_screen(uint32_t *argb);
void quorum_crt(uint8_t *intensity, uint8_t *rgba, int32_t *gain);
void quorum_touch(int32_t act, float x, float y);
int32_t quorum_name_ask(char *out);
void quorum_route(int32_t mode);
uint32_t quorum_row(int32_t g);
void quorum_cell(int32_t g, int32_t c);
int32_t quorum_route_get(void);
void quorum_strike(int32_t s, int32_t down);
int32_t quorum_strike_lit(void);
int32_t quorum_fg_level(void);
int32_t quorum_booting(void);
void quorum_vintage(int32_t on);
int32_t quorum_vintage_get(void);
void quorum_name_set(const char *s, int32_t ok);
}

#define TAG "Quorum"

namespace {

bool openAndStart();

class Callback : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
public:
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *, void *data, int32_t frames) override {
        quorum_pull(static_cast<float *>(data), frames);
        return oboe::DataCallbackResult::Continue;
    }
    void onErrorAfterClose(oboe::AudioStream *, oboe::Result error) override {
        // headphones in or out, a USB interface: the stream closes; open a new one on the new device
        __android_log_print(ANDROID_LOG_INFO, TAG, "stream closed (%s), reopening", oboe::convertToText(error));
        if (error == oboe::Result::ErrorDisconnected) openAndStart();
    }
};

Callback gCallback;
std::shared_ptr<oboe::AudioStream> gStream;
std::mutex gLock;
bool gWanted = false;

bool openAndStart() {
    std::lock_guard<std::mutex> l(gLock);
    if (!gWanted) return false;
    oboe::AudioStreamBuilder b;
    b.setDirection(oboe::Direction::Output)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Shared)
        ->setFormat(oboe::AudioFormat::Float)
        ->setChannelCount(oboe::ChannelCount::Stereo)
        ->setSampleRate(44100)   // the engine's rate, as the FM-1 sends it; Oboe converts if the phone runs at 48k
        ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
        ->setUsage(oboe::Usage::Media)
        ->setContentType(oboe::ContentType::Music)
        ->setDataCallback(&gCallback)
        ->setErrorCallback(&gCallback);
    oboe::Result r = b.openStream(gStream);
    if (r != oboe::Result::OK) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "openStream: %s", oboe::convertToText(r));
        return false;
    }
    r = gStream->requestStart();
    if (r != oboe::Result::OK) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "requestStart: %s", oboe::convertToText(r));
        return false;
    }
    __android_log_print(ANDROID_LOG_INFO, TAG, "audio: %d Hz, %d frames a burst",
                        gStream->getSampleRate(), gStream->getFramesPerBurst());
    return true;
}

void stopAndClose() {
    std::lock_guard<std::mutex> l(gLock);
    if (gStream) {
        gStream->stop();
        gStream->close();
        gStream.reset();
    }
}

}  // namespace

#define JNI extern "C" JNIEXPORT
#define FN(name) Java_com_zeptosecond_quorum_Native_##name

JNI void JNICALL FN(start)(JNIEnv *env, jclass, jstring dir) {
    const char *d = env->GetStringUTFChars(dir, nullptr);
    quorum_start(d);
    env->ReleaseStringUTFChars(dir, d);
}
JNI jboolean JNICALL FN(audioStart)(JNIEnv *, jclass) {
    { std::lock_guard<std::mutex> l(gLock); gWanted = true; if (gStream) return JNI_TRUE; }
    return openAndStart() ? JNI_TRUE : JNI_FALSE;
}
JNI void JNICALL FN(audioStop)(JNIEnv *, jclass) {
    { std::lock_guard<std::mutex> l(gLock); gWanted = false; }
    stopAndClose();
}
JNI void JNICALL FN(setLatency)(JNIEnv *, jclass, jint frames) { quorum_set_latency((uint32_t)frames); }
JNI jint JNICALL FN(underruns)(JNIEnv *, jclass) { return (jint)quorum_underruns(); }
JNI void JNICALL FN(button)(JNIEnv *, jclass, jint id, jboolean down) { quorum_button(id, down ? 1 : 0); }
JNI void JNICALL FN(enc)(JNIEnv *, jclass, jint id, jint detents) { quorum_enc(id, detents); }
JNI void JNICALL FN(keys)(JNIEnv *, jclass, jint mask) { quorum_keys((uint32_t)mask); }
JNI void JNICALL FN(midi)(JNIEnv *, jclass, jint st, jint d1, jint d2) {
    quorum_midi((uint8_t)st, (uint8_t)d1, (uint8_t)d2);
}
JNI jlong JNICALL FN(leds)(JNIEnv *, jclass) { return (jlong)quorum_leds(); }
JNI jlong JNICALL FN(glow)(JNIEnv *, jclass) { return (jlong)quorum_glow(); }
JNI void JNICALL FN(screen)(JNIEnv *env, jclass, jintArray out) {
    if (env->GetArrayLength(out) < 240 * 240) return;
    jint *p = env->GetIntArrayElements(out, nullptr);
    quorum_copy_screen(reinterpret_cast<uint32_t *>(p));
    env->ReleaseIntArrayElements(out, p, 0);
}
// the tube's inputs for CrtView: intensities (240 x 240 bytes), the phosphor ramp (256 RGBA), the gain, scanlines, degauss, capacitor and flyback (7 ints)
JNI void JNICALL FN(crt)(JNIEnv *env, jclass, jobject intensity, jobject ramp, jintArray gain) {
    auto *in = static_cast<uint8_t *>(env->GetDirectBufferAddress(intensity));
    auto *rg = static_cast<uint8_t *>(env->GetDirectBufferAddress(ramp));
    if (!in || !rg || env->GetDirectBufferCapacity(intensity) < 240 * 240 || env->GetDirectBufferCapacity(ramp) < 1024) return;
    if (env->GetArrayLength(gain) < 7) return;
    jint g[7];
    quorum_crt(in, rg, reinterpret_cast<int32_t *>(g));
    env->SetIntArrayRegion(gain, 0, 7, g);
}
// a finger on the screen: act 0 down, 1 move, 2 up; x, y on the glass in the firmware's pixels (0 .. 240)
JNI void JNICALL FN(touch)(JNIEnv *, jclass, jint act, jfloat x, jfloat y) { quorum_touch(act, x, y); }
// the Disk's naming on the phone's keyboard: the name the engine wants typed (null: none), and the answer
JNI jstring JNICALL FN(nameAsk)(JNIEnv *env, jclass) {
    char n[9] = {0};
    if (!quorum_name_ask(n)) return nullptr;
    n[8] = 0;
    return env->NewStringUTF(n);
}
JNI void JNICALL FN(nameSet)(JNIEnv *env, jclass, jstring name, jboolean ok) {
    char n[9] = {0};
    if (name) {
        const char *s = env->GetStringUTFChars(name, nullptr);
        for (int i = 0; i < 8 && s[i]; i++) n[i] = s[i];
        env->ReleaseStringUTFChars(name, s);
    }
    quorum_name_set(n, ok ? 1 : 0);
}
// the buttons over the keyboard: the split the keys play (1 A, 2 A+B, 3 B), strike 1 and 2 by hand
JNI void JNICALL FN(route)(JNIEnv *, jclass, jint mode) { quorum_route(mode); }
JNI jint JNICALL FN(routeGet)(JNIEnv *, jclass) { return quorum_route_get(); }
JNI void JNICALL FN(strike)(JNIEnv *, jclass, jint s, jboolean down) { quorum_strike(s, down ? 1 : 0); }
JNI jint JNICALL FN(strikeLit)(JNIEnv *, jclass) { return quorum_strike_lit(); }
JNI jint JNICALL FN(fgLevel)(JNIEnv *, jclass) { return quorum_fg_level(); }
JNI jint JNICALL FN(booting)(JNIEnv *, jclass) { return quorum_booting(); }
JNI void JNICALL FN(vintage)(JNIEnv *, jclass, jboolean on) { quorum_vintage(on ? 1 : 0); }
JNI jboolean JNICALL FN(vintageGet)(JNIEnv *, jclass) { return quorum_vintage_get() != 0; }
JNI jint JNICALL FN(row)(JNIEnv *, jclass, jint g) { return (jint)quorum_row(g); }
JNI void JNICALL FN(cell)(JNIEnv *, jclass, jint g, jint c) { quorum_cell(g, c); }
