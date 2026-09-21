/*
 * xournal-qt: shadow of upstream's geometry tool input handler.
 *
 * Upstream's handler is built on GTK events; the Qt build has its own input (see qt/src/canvas/GeometryToolLayer).
 * The model only needs the name for its dispatch pool, so this declares it and nothing else.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

class GeometryToolInputHandler {
public:
    virtual ~GeometryToolInputHandler() = default;
};
