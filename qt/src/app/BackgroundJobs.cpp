#include "BackgroundJobs.h"

#include <algorithm>

namespace xqt {

BackgroundJobs::BackgroundJobs() {
    // (as many as the global pool would give, at least two: a long export does not hold up a tag written meanwhile)
    pool.setMaxThreadCount(std::max(2, QThread::idealThreadCount()));
    pool.setObjectName(QStringLiteral("xqt-background-jobs"));
}

BackgroundJobs::~BackgroundJobs() { pool.waitForDone(); }

void BackgroundJobs::waitForDone() { pool.waitForDone(); }

}  // namespace xqt
