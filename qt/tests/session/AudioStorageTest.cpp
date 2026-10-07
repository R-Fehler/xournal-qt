/*
 * xournal-qt: where recordings are kept (qt/docs/features/audio.md, "Storage"): found in the app's audio folder and
 * next to the document; carried by a PDF with notes as attachments named with their pages ("audio-p001-p003-….ogg",
 * renamed by an incremental save when pages move), left out of the clean copy, taken out again when the PDF is opened;
 * an archive PDF's associated files; "Export for Xournal++" with the recordings copied beside it under absolute names.
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>
#include <fstream>
#include <memory>
#include <numbers>
#include <sstream>

#include <QProcess>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFJob.hh>
#include <qpdf/QPDFLogger.hh>

#include "audio/AudioFiles.h"
#include "audio/DocumentAudio.h"
#include "audio/OggVorbis.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "undo/UndoRedoHandler.h"

using namespace xqt;

namespace {
constexpr const char* NAME = "2026-10-04_10-00-00.ogg";

std::string bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

int qpdfCheck(const fs::path& pdf) {
    std::ostringstream out, err;
    QPDFJob job;
    auto logger = QPDFLogger::create();
    logger->setOutputStreams(&out, &err);
    job.setLogger(logger);
    const std::string file = pdf.string();
    const char* argv[] = {"qpdf", "--check", file.c_str(), nullptr};
    job.initializeFromArgv(argv);
    job.run();
    return job.getExitCode();
}

/// The embedded files of a PDF: name -> (data, MIME type, object of the stream)
struct Embedded {
    std::string data, mime;
    QPDFObjGen stream;
    std::string relationship;
};
std::map<std::string, Embedded> embeddedOf(const fs::path& pdf) {
    QPDF q;
    q.setSuppressWarnings(true);
    q.processFile(pdf.string().c_str());
    std::map<std::string, Embedded> out;
    for (const auto& [name, spec]: QPDFEmbeddedFileDocumentHelper(q).getEmbeddedFiles()) {
        QPDFObjectHandle s = spec->getEmbeddedFileStream();
        auto buffer = s.getStreamData(qpdf_dl_all);
        Embedded e;
        e.data.assign(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize());
        QPDFObjectHandle sub = s.getDict().getKey("/Subtype");
        e.mime = sub.isName() ? sub.getName() : "";
        e.stream = s.getObjGen();
        QPDFObjectHandle rel = spec->getObjectHandle().getKey("/AFRelationship");
        e.relationship = rel.isName() ? rel.getName() : "";
        out[name] = e;
    }
    return out;
}

std::vector<std::string> audioMarkerOf(const fs::path& pdf) {
    QPDF q;
    q.setSuppressWarnings(true);
    q.processFile(pdf.string().c_str());
    std::vector<std::string> out;
    QPDFObjectHandle list = q.getRoot().getKey("/XournalQt").getKey("/Audio");
    for (int i = 0; list.isArray() && i < list.getArrayNItems(); ++i) {
        out.push_back(list.getArrayItem(i).getUTF8Value());
    }
    return out;
}

class AudioStorageTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        audio::setAppFolder(path("app-audio"));
        recording = audio::appFolder() / NAME;
        audio::VorbisWriter w;
        ASSERT_TRUE(w.open(recording, 48000));
        std::vector<float> v(48000);
        for (size_t i = 0; i < v.size(); ++i) {
            v[i] = 0.3f * static_cast<float>(std::sin(2 * std::numbers::pi * 440 * static_cast<double>(i) / 48000));
        }
        w.write(v.data(), v.size());
        ASSERT_TRUE(w.close());
    }
    void TearDown() override {
        audio::setAppFolder({});
        audio::setExtraFolders({});
    }
    fs::path path(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }
    /// Three pages: the recording is a memo of page 1 and a stroke of page 3 was written while it ran.
    std::unique_ptr<DocumentSession> recorded() {
        auto s = std::make_unique<DocumentSession>(*app);
        s->insertNewPage(1);
        s->insertNewPage(2);
        EXPECT_TRUE(s->addVoiceMemo(0, NAME));
        {
            std::unique_lock lock(*s->getDocument());
            auto st = std::make_unique<Stroke>();
            st->setWidth(1.5);
            st->addPoint(Point(100, 100));
            st->addPoint(Point(200, 130));
            audio::stamp(*st, NAME, 700);
            s->getDocument()->getPage(2)->getSelectedLayer()->addElement(std::move(st));
        }
        return s;
    }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    fs::path recording;
};
}  // namespace

TEST_F(AudioStorageTest, recordingsAreFoundWhereUpstreamAndTheAppKeepThem) {
    const fs::path doc = path("notes/lecture.xopp");
    fs::create_directories(doc.parent_path() / "lecture.audio");
    fs::create_directories(path("xournalpp-audio"));
    std::ofstream(doc.parent_path() / "beside.ogg") << "x";
    std::ofstream(doc.parent_path() / "lecture.audio" / "exported.ogg") << "x";
    std::ofstream(path("xournalpp-audio/upstream.ogg")) << "x";
    EXPECT_EQ(audio::find(NAME, doc), recording);
    EXPECT_EQ(audio::find(NAME), recording);
    EXPECT_EQ(audio::find("beside.ogg", doc), doc.parent_path() / "beside.ogg");
    EXPECT_EQ(audio::find("exported.ogg", doc), doc.parent_path() / "lecture.audio" / "exported.ogg");
    EXPECT_TRUE(audio::find("upstream.ogg", doc).empty());
    audio::setExtraFolders({path("xournalpp-audio")});
    EXPECT_EQ(audio::find("upstream.ogg", doc), path("xournalpp-audio/upstream.ogg"));
    // An absolute name (Export for Xournal++): as it is, else by its file name
    const std::u8string abs = recording.u8string();
    EXPECT_EQ(audio::find(std::string(abs.begin(), abs.end())), recording);
    const std::u8string gone = path("elsewhere/lecture.audio/beside.ogg").u8string();
    EXPECT_EQ(audio::find(std::string(gone.begin(), gone.end()), doc), doc.parent_path() / "beside.ogg");
    EXPECT_TRUE(audio::find("missing.ogg", doc).empty());
    EXPECT_TRUE(audio::find("").empty());

    EXPECT_EQ(audio::attachmentName(NAME, {11}), "audio-p012-2026-10-04_10-00-00.ogg");
    EXPECT_EQ(audio::attachmentName(NAME, {11, 12, 14}), "audio-p012-p015-2026-10-04_10-00-00.ogg");
    EXPECT_EQ(audio::attachmentName(NAME, {1233}), "audio-p1234-2026-10-04_10-00-00.ogg");
    EXPECT_EQ(audio::attachmentName("/abs/path/x.ogg", {0}), "audio-p001-x.ogg");
    EXPECT_EQ(audio::exportFolderOf(doc), doc.parent_path() / "lecture.audio");
}

// The PDF carries the recording under a name with its pages, as audio/ogg, its bytes unchanged; the clean copy does
// not; opened again (the app's file gone), the recording is found in what was taken out of the PDF
TEST_F(AudioStorageTest, aPdfWithNotesCarriesItsRecordings) {
    auto s = recorded();
    const fs::path pdf = path("notes.pdf");
    const auto r = s->saveAsHybrid(pdf);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(qpdfCheck(pdf), 0);
    const auto files = embeddedOf(pdf);
    const std::string attached = "audio-p001-p003-2026-10-04_10-00-00.ogg";
    ASSERT_TRUE(files.count(attached)) << "attachments: " << files.size();
    EXPECT_EQ(files.at(attached).data, bytes(recording));
    EXPECT_EQ(files.at(attached).mime, "/audio/ogg");
    EXPECT_EQ(audioMarkerOf(pdf), (std::vector<std::string>{attached, NAME}));

    fs::remove(recording);
    auto opened = HybridPdf::open(pdf);
    ASSERT_TRUE(opened.document) << opened.error;
    EXPECT_EQ(bytes(opened.audio / NAME), files.at(attached).data);
    for (const auto& [name, e]: embeddedOf(opened.base)) {
        EXPECT_EQ(name.find("audio-"), std::string::npos) << "the clean copy carries no recording: " << name;
    }
    const fs::path found = audio::find(NAME, pdf);
    ASSERT_FALSE(found.empty());
    EXPECT_EQ(bytes(found), files.at(attached).data);
    EXPECT_EQ(audio::recordingsOf(*opened.document).size(), 1u);
}

// Pages moved: Ctrl+S renames the attachment (the same stream, nothing of the recording appended again); the
// recording removed from the document: written in full without it
TEST_F(AudioStorageTest, anIncrementalSaveRenamesTheAttachmentWhenPagesMove) {
    HybridPdf::compactAbove = 1000;
    auto s = recorded();
    const fs::path pdf = path("notes.pdf");
    ASSERT_TRUE(s->saveAsHybrid(pdf).ok);
    const auto before = embeddedOf(pdf);
    const auto stream = before.at("audio-p001-p003-2026-10-04_10-00-00.ogg").stream;
    const auto size = fs::file_size(pdf);

    ASSERT_TRUE(s->movePages({0}, 3));  // (the memo's page goes last: the recording is on pages 2 and 3)
    auto r = s->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.incremental);
    EXPECT_LT(r.appended, fs::file_size(recording) + size / 2) << "the recording was not appended again";
    EXPECT_EQ(qpdfCheck(pdf), 0);
    auto after = embeddedOf(pdf);
    EXPECT_FALSE(after.count("audio-p001-p003-2026-10-04_10-00-00.ogg"));
    ASSERT_TRUE(after.count("audio-p002-p003-2026-10-04_10-00-00.ogg"));
    EXPECT_EQ(after.at("audio-p002-p003-2026-10-04_10-00-00.ogg").stream, stream);
    EXPECT_EQ(after.at("audio-p002-p003-2026-10-04_10-00-00.ogg").data, bytes(recording));
    EXPECT_EQ(audioMarkerOf(pdf), (std::vector<std::string>{"audio-p002-p003-2026-10-04_10-00-00.ogg", NAME}));

    EXPECT_EQ(s->removeRecording(NAME), 2u);
    r = s->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_FALSE(r.incremental);
    after = embeddedOf(pdf);
    for (const auto& [name, e]: after) {
        EXPECT_EQ(name.find("audio-"), std::string::npos) << name;
    }
    EXPECT_TRUE(audioMarkerOf(pdf).empty());
    EXPECT_EQ(qpdfCheck(pdf), 0);
    HybridPdf::compactAbove = 0.25;
}

TEST_F(AudioStorageTest, anArchivePdfListsItAsAnAssociatedFile) {
    auto s = recorded();
    const fs::path pdf = path("notes.archive.pdf");
    const auto r = s->exportArchive(pdf);
    ASSERT_TRUE(r.ok) << r.error;
    const auto files = embeddedOf(pdf);
    ASSERT_TRUE(files.count("audio-p001-p003-2026-10-04_10-00-00.ogg"));
    EXPECT_EQ(files.at("audio-p001-p003-2026-10-04_10-00-00.ogg").relationship, "/Supplement");
    QPDF q;
    q.processFile(pdf.string().c_str());
    QPDFObjectHandle af = q.getRoot().getKey("/AF");
    bool listed = false;
    for (int i = 0; af.isArray() && i < af.getArrayNItems(); ++i) {
        listed = listed || af.getArrayItem(i).getKey("/UF").getUTF8Value() == "audio-p001-p003-2026-10-04_10-00-00.ogg";
    }
    EXPECT_TRUE(listed);
}

// Export for Xournal++: the recordings copied into "name.audio" beside the .xopp, its strokes naming them by their
// absolute paths (Xournal++ plays an absolute name as it is)
TEST_F(AudioStorageTest, exportForXournalppCopiesTheRecordings) {
    auto s = recorded();
    const fs::path xopp = path("export/lecture.xopp");
    fs::create_directories(xopp.parent_path());
    const auto r = s->exportXopp(xopp);
    ASSERT_TRUE(r.ok) << r.error << r.exportError;
    const fs::path copy = xopp.parent_path() / "lecture.audio" / NAME;
    ASSERT_TRUE(fs::exists(copy));
    EXPECT_EQ(bytes(copy), bytes(recording));
    QProcess gz;
    gz.start("gzip", {"-dc", QString::fromStdString(xopp.string())});
    ASSERT_TRUE(gz.waitForFinished());
    const QString xml = QString::fromUtf8(gz.readAllStandardOutput());
    EXPECT_TRUE(xml.contains(QString("fn=\"%1\"").arg(QString::fromStdString(fs::absolute(copy).generic_string()))))
            << xml.toStdString();
    // The document itself keeps the bare name
    EXPECT_EQ(audio::recordingsOf(*s->getDocument()).front().name, NAME);
}

// A PDF with notes saved as a .xopp: its recordings go into the app's audio folder, where the .xopp finds them
TEST_F(AudioStorageTest, savedAsXoppTheRecordingsGoToTheAppFolder) {
    {
        auto s = recorded();
        ASSERT_TRUE(s->saveAsHybrid(path("notes.pdf")).ok);
    }
    fs::remove(recording);
    auto loaded = DocumentSession::loadFile(path("notes.pdf"));
    ASSERT_TRUE(loaded.document) << loaded.error;
    DocumentSession s(*app, std::move(loaded.document));
    fs::create_directories(path("other"));
    const auto r = s.saveAs(path("other/notes.xopp"));
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_TRUE(fs::exists(recording));
    EXPECT_EQ(audio::durationMsOf(recording), 1000);
}
