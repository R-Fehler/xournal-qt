/*
 * xournal-qt: the worker threads that draw, read and store pictures for the image providers and their caches, with
 * one owner.
 *
 * Each kind of work has a pool of its own, so one kind does not hold up another (a fling through the library grid
 * does not delay the page sidebar); the size of each pool is in one table (ImageWorkers.cpp). All of them run at idle
 * priority (SCHED_IDLE on Linux and Android): none of these pictures is in front of the page the reader is looking at,
 * which the canvas's own renders (RenderService) draw first.
 *
 * shutdown() stops all of them before the application takes its plugins away (they draw and save with Qt's image
 * plugins, Cairo, Pango and poppler): what is queued is dropped, what runs is waited for, and nothing starts any more
 * (start() returns false; respond() finishes the response without an image).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>

#include <QImage>

namespace xqt {

class AsyncImageResponse;

class ImageWorkers {
public:
    enum class Pool {
        Thumbnails,          ///< sharp page thumbnails (ThumbnailProvider)
        Sketches,            ///< sketches and stand-ins of the open documents' pages (PageSketches)
        SketchFiles,         ///< storing stand-ins on disk and trimming them (PageSketches)
        Covers,              ///< documents' covers in the library and recent-files grids (DocumentCovers)
        CoverFiles,          ///< writing the covers' packs
        HitPages,            ///< pages with search hits (HitPageProvider)
        Snippets,            ///< Markdown snippet cards (MdSnippetProvider)
        Annotations,         ///< reading a document's annotations (AnnotationsModel)
        AnnotationPictures,  ///< pictures of handwriting in the annotations panel (AnnotationImageProvider)
        Count
    };

    /// Run `job` on a worker of `pool` (a higher `priority` first). False when the workers are shut down: it is not
    /// run.
    static bool start(Pool pool, std::function<void()> job, int priority = 0);
    /// Make `response`'s image on a worker of `pool` with `make`, which is not called when QML cancelled the response
    /// before the worker began. The response is finished in any case (with no image when cancelled or shut down).
    static void respond(Pool pool, AsyncImageResponse* response, std::function<QImage()> make, int priority = 0);
    /// Wait until the jobs of `pool` are done.
    static void waitForDone(Pool pool);
    /// Drop the queued jobs of every pool, wait for the running ones; nothing starts afterwards.
    static void shutdown();
    static bool isShutDown();
    /// Jobs running or queued in all pools (tests).
    static int busy();
    /// (tests) Accept work again after shutdown().
    static void reopen();
    /// Threads of a pool (tests, measurements).
    static int threadsOf(Pool pool);
};

}  // namespace xqt
