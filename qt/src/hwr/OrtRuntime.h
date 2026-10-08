/*
 * xournal-qt: ONNX Runtime, loaded when it is needed (qt/docs/features/handwriting-search.md).
 *
 * The app is built without linking ONNX Runtime: only its C API's headers are vendored (qt/3rdparty/onnxruntime). The
 * library is opened (dlopen) the first time the handwriting search wants a model: XQT_ONNXRUNTIME (a path), else where
 * the packages put it next to the program (candidates(): Linux "<app>/../lib/xournal-qt/libonnxruntime.so.1", Windows
 * "<app>/onnxruntime.dll", macOS "<app>/../Frameworks/libonnxruntime.1.dylib" in the app bundle), else the system's
 * by its name; on Android "libonnxruntime.so" by its name (the AAR's library among the APK's native libraries). Without
 * it the handwriting search says so in Settings and nothing else changes. The app asks for C API version 16, so ONNX
 * Runtime 1.16 or newer works.
 *
 * Session wraps one model with the few calls the recogniser needs: its inputs' names and shapes, and a run with
 * float, int64 and bool tensors whose outputs are copied out (float).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <QString>

struct OrtApi;
struct OrtSession;
struct OrtRunOptions;

namespace xqt::hwr::ort {

constexpr uint32_t API_VERSION = 16;

/// The runtime's API (loaded once); nullptr and `why` if it cannot be loaded.
const OrtApi* api(QString* why = nullptr);
/// The runtime loaded (after api()): the path or name it was opened by and its version ("1.20.1"); "" if none.
QString loadedPath();
QString version();

enum class Platform { Linux, Windows, MacOS, Android };
Platform thisPlatform();
/// Where the runtime is looked for, in this order (`appDir`: the program's folder, "" if unknown; `env`:
/// XQT_ONNXRUNTIME, which alone is tried when set). A name without a folder is the system's search.
QStringList candidates(Platform platform, const QString& appDir, const QString& env);

struct Tensor {
    enum class Type { Float, Int64, Bool };
    Type type = Type::Float;
    std::vector<int64_t> shape;
    std::vector<float> f;
    std::vector<int64_t> i;
    std::vector<uint8_t> b;
    size_t elements() const;
    static Tensor floats(std::vector<int64_t> shape, std::vector<float> data = {});
    static Tensor ints(std::vector<int64_t> shape, std::vector<int64_t> data);
    static Tensor bools(std::vector<int64_t> shape, std::vector<uint8_t> data);
};

class Session {
public:
    /// A model file run with `threads` threads (no spinning, no memory arena); nullptr and `why` if it fails.
    static std::unique_ptr<Session> open(const QString& file, int threads, QString* why);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    std::vector<std::string> inputs, outputs;
    std::vector<std::vector<int64_t>> inputShapes;  ///< per input (-1: any)

    /// Run; `outputs` get the named outputs (float). `options` may be null. False and `why` on failure (also when it
    /// was interrupted through `options`).
    bool run(const std::vector<std::pair<std::string, const Tensor*>>& in, const std::vector<std::string>& names,
             std::vector<Tensor>& out, OrtRunOptions* options, QString* why);

private:
    Session() = default;
    OrtSession* session = nullptr;
};

/// Run options that can be interrupted from another thread.
class RunOptions {
public:
    RunOptions();
    ~RunOptions();
    OrtRunOptions* get() const { return options; }
    void terminate();
    void reset();

private:
    OrtRunOptions* options = nullptr;
};

}  // namespace xqt::hwr::ort
