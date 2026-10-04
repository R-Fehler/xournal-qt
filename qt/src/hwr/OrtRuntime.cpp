#include "OrtRuntime.h"

#include <mutex>

#include <QCoreApplication>
#include <QFileInfo>

#include "onnxruntime_c_api.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace xqt::hwr::ort {

namespace {
struct Loaded {
    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;
    QString path;
    QString why;
};

#ifdef _WIN32
const char* LIB_NAME = "onnxruntime.dll";
#elif defined(__APPLE__)
const char* LIB_NAME = "libonnxruntime.1.dylib";
#else
const char* LIB_NAME = "libonnxruntime.so.1";
#endif

void* openLibrary(const QString& path) {
#ifdef _WIN32
    return reinterpret_cast<void*>(LoadLibraryW(reinterpret_cast<const wchar_t*>(path.utf16())));
#else
    return dlopen(path.toLocal8Bit().constData(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void* symbol(void* lib, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), name));
#else
    return dlsym(lib, name);
#endif
}

Loaded& loaded() {
    static Loaded l = [] {
        Loaded r;
        QStringList candidates;
        if (const QString env = qEnvironmentVariable("XQT_ONNXRUNTIME"); !env.isEmpty()) {
            candidates << env;
        } else {
            if (QCoreApplication::instance()) {
                const QString dir = QCoreApplication::applicationDirPath();
                candidates << dir + QStringLiteral("/../lib/xournal-qt/") + QLatin1String(LIB_NAME)
                           << dir + u'/' + QLatin1String(LIB_NAME);
            }
            candidates << QString::fromLatin1(LIB_NAME);
        }
        void* lib = nullptr;
        for (const QString& c: candidates) {
            if (c.contains(u'/') && !QFileInfo::exists(c)) {
                continue;
            }
            if ((lib = openLibrary(c))) {
                r.path = c;
                break;
            }
        }
        if (!lib) {
            r.path = candidates.join(QStringLiteral(", "));
            r.why = QStringLiteral("ONNX Runtime (%1) is not installed").arg(QLatin1String(LIB_NAME));
            return r;
        }
        using GetApiBase = const OrtApiBase* (*)();
        auto getBase = reinterpret_cast<GetApiBase>(symbol(lib, "OrtGetApiBase"));
        const OrtApiBase* base = getBase ? getBase() : nullptr;
        r.api = base ? base->GetApi(API_VERSION) : nullptr;
        if (!r.api) {
            r.why = QStringLiteral("ONNX Runtime at %1 is too old (1.16 or newer is needed)").arg(r.path);
            return r;
        }
        if (OrtStatus* s = r.api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "xournal-qt", &r.env)) {
            r.why = QString::fromUtf8(r.api->GetErrorMessage(s));
            r.api->ReleaseStatus(s);
            r.api = nullptr;
        }
        return r;
    }();
    return l;
}

/// The message of a failed call (and the status released); "" if it did not fail.
QString failed(OrtStatus* s) {
    if (!s) {
        return {};
    }
    const OrtApi* a = loaded().api;
    QString msg = QString::fromUtf8(a->GetErrorMessage(s));
    a->ReleaseStatus(s);
    return msg.isEmpty() ? QStringLiteral("ONNX Runtime failed") : msg;
}
}  // namespace

const OrtApi* api(QString* why) {
    static std::mutex mtx;
    std::lock_guard lock(mtx);
    Loaded& l = loaded();
    if (!l.api && why) {
        *why = l.why;
    }
    return l.api;
}

QString libraryPath() { return loaded().path; }

size_t Tensor::elements() const {
    size_t n = 1;
    for (const int64_t d: shape) {
        n *= static_cast<size_t>(std::max<int64_t>(0, d));
    }
    return n;
}

Tensor Tensor::floats(std::vector<int64_t> shape, std::vector<float> data) {
    Tensor t;
    t.type = Type::Float;
    t.shape = std::move(shape);
    t.f = std::move(data);
    t.f.resize(t.elements());
    return t;
}

Tensor Tensor::ints(std::vector<int64_t> shape, std::vector<int64_t> data) {
    Tensor t;
    t.type = Type::Int64;
    t.shape = std::move(shape);
    t.i = std::move(data);
    t.i.resize(t.elements());
    return t;
}

Tensor Tensor::bools(std::vector<int64_t> shape, std::vector<uint8_t> data) {
    Tensor t;
    t.type = Type::Bool;
    t.shape = std::move(shape);
    t.b = std::move(data);
    t.b.resize(t.elements());
    return t;
}

std::unique_ptr<Session> Session::open(const QString& file, int threads, QString* why) {
    const OrtApi* a = api(why);
    if (!a) {
        return nullptr;
    }
    OrtSessionOptions* options = nullptr;
    QString error = failed(a->CreateSessionOptions(&options));
    if (error.isEmpty()) {
        error = failed(a->SetIntraOpNumThreads(options, threads));
    }
    if (error.isEmpty()) {
        error = failed(a->SetInterOpNumThreads(options, 1));
    }
    if (error.isEmpty()) {
        // (a background worker: its threads sleep when they have nothing to do)
        error = failed(a->AddSessionConfigEntry(options, "session.intra_op.allow_spinning", "0"));
    }
    if (error.isEmpty()) {
        error = failed(a->AddSessionConfigEntry(options, "session.inter_op.allow_spinning", "0"));
    }
    if (error.isEmpty()) {
        error = failed(a->DisableCpuMemArena(options));
    }
    if (error.isEmpty()) {
        error = failed(a->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL));
    }
    std::unique_ptr<Session> s(new Session);
    if (error.isEmpty()) {
#ifdef _WIN32
        const std::wstring path = file.toStdWString();
#else
        const QByteArray path = file.toLocal8Bit();
#endif
        error = failed(a->CreateSession(loaded().env, path.data(), options, &s->session));
    }
    if (options) {
        a->ReleaseSessionOptions(options);
    }
    OrtAllocator* allocator = nullptr;
    if (error.isEmpty()) {
        error = failed(a->GetAllocatorWithDefaultOptions(&allocator));
    }
    size_t count = 0;
    if (error.isEmpty()) {
        error = failed(a->SessionGetInputCount(s->session, &count));
    }
    for (size_t i = 0; error.isEmpty() && i < count; ++i) {
        char* name = nullptr;
        error = failed(a->SessionGetInputName(s->session, i, allocator, &name));
        if (!error.isEmpty()) {
            break;
        }
        s->inputs.emplace_back(name);
        a->AllocatorFree(allocator, name);
        std::vector<int64_t> dims;
        OrtTypeInfo* info = nullptr;
        if (failed(a->SessionGetInputTypeInfo(s->session, i, &info)).isEmpty()) {
            const OrtTensorTypeAndShapeInfo* tensor = nullptr;
            size_t n = 0;
            if (failed(a->CastTypeInfoToTensorInfo(info, &tensor)).isEmpty() && tensor &&
                failed(a->GetDimensionsCount(tensor, &n)).isEmpty()) {
                dims.resize(n);
                failed(a->GetDimensions(tensor, dims.data(), n));
            }
            a->ReleaseTypeInfo(info);
        }
        s->inputShapes.push_back(std::move(dims));
    }
    count = 0;
    if (error.isEmpty()) {
        error = failed(a->SessionGetOutputCount(s->session, &count));
    }
    for (size_t i = 0; error.isEmpty() && i < count; ++i) {
        char* name = nullptr;
        error = failed(a->SessionGetOutputName(s->session, i, allocator, &name));
        if (error.isEmpty()) {
            s->outputs.emplace_back(name);
            a->AllocatorFree(allocator, name);
        }
    }
    if (!error.isEmpty()) {
        if (why) {
            *why = error;
        }
        return nullptr;
    }
    return s;
}

Session::~Session() {
    if (session) {
        loaded().api->ReleaseSession(session);
    }
}

bool Session::run(const std::vector<std::pair<std::string, const Tensor*>>& in, const std::vector<std::string>& names,
                  std::vector<Tensor>& out, OrtRunOptions* options, QString* why) {
    const OrtApi* a = loaded().api;
    OrtMemoryInfo* memory = nullptr;
    QString error = failed(a->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory));
    std::vector<OrtValue*> values;
    std::vector<const char*> inNames;
    static float none = 0;  // (the data of an empty tensor)
    for (const auto& [name, t]: in) {
        if (!error.isEmpty()) {
            break;
        }
        OrtValue* v = nullptr;
        void* data = &none;
        size_t bytes = 0;
        ONNXTensorElementDataType type = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
        switch (t->type) {
            case Tensor::Type::Float:
                data = t->f.empty() ? data : const_cast<float*>(t->f.data());
                bytes = t->f.size() * sizeof(float);
                break;
            case Tensor::Type::Int64:
                data = t->i.empty() ? data : const_cast<int64_t*>(t->i.data());
                bytes = t->i.size() * sizeof(int64_t);
                type = ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64;
                break;
            case Tensor::Type::Bool:
                data = t->b.empty() ? data : const_cast<uint8_t*>(t->b.data());
                bytes = t->b.size();
                type = ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL;
                break;
        }
        error = failed(a->CreateTensorWithDataAsOrtValue(memory, data, bytes, t->shape.data(), t->shape.size(), type, &v));
        values.push_back(v);
        inNames.push_back(name.c_str());
    }
    std::vector<OrtValue*> results(names.size(), nullptr);
    std::vector<const char*> outNames;
    for (const std::string& n: names) {
        outNames.push_back(n.c_str());
    }
    if (error.isEmpty()) {
        error = failed(a->Run(session, options, inNames.data(), values.data(), values.size(), outNames.data(),
                              outNames.size(), results.data()));
    }
    out.clear();
    for (size_t k = 0; error.isEmpty() && k < results.size(); ++k) {
        OrtTensorTypeAndShapeInfo* info = nullptr;
        error = failed(a->GetTensorTypeAndShape(results[k], &info));
        Tensor t;
        size_t n = 0;
        if (error.isEmpty()) {
            error = failed(a->GetDimensionsCount(info, &n));
        }
        if (error.isEmpty()) {
            t.shape.resize(n);
            error = failed(a->GetDimensions(info, t.shape.data(), n));
        }
        if (info) {
            a->ReleaseTensorTypeAndShapeInfo(info);
        }
        void* data = nullptr;
        if (error.isEmpty()) {
            error = failed(a->GetTensorMutableData(results[k], &data));
        }
        if (error.isEmpty()) {
            const auto* f = static_cast<const float*>(data);
            t.f.assign(f, f + t.elements());
            out.push_back(std::move(t));
        }
    }
    for (OrtValue* v: results) {
        if (v) {
            a->ReleaseValue(v);
        }
    }
    for (OrtValue* v: values) {
        if (v) {
            a->ReleaseValue(v);
        }
    }
    if (memory) {
        a->ReleaseMemoryInfo(memory);
    }
    if (!error.isEmpty() && why) {
        *why = error;
    }
    return error.isEmpty();
}

RunOptions::RunOptions() {
    if (const OrtApi* a = api()) {
        failed(a->CreateRunOptions(&options));
    }
}

RunOptions::~RunOptions() {
    if (options) {
        loaded().api->ReleaseRunOptions(options);
    }
}

void RunOptions::terminate() {
    if (options) {
        failed(loaded().api->RunOptionsSetTerminate(options));
    }
}

void RunOptions::reset() {
    if (options) {
        failed(loaded().api->RunOptionsUnsetTerminate(options));
    }
}

}  // namespace xqt::hwr::ort
