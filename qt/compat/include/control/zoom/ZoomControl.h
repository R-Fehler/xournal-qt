/*
 * xournal-qt: shadow of upstream control/zoom/ZoomControl.h.
 *
 * The zoom itself (gestures, fit, limits) is the canvas' ViewController; this class only gives reused upstream code
 * (spline tool, selection) the current values and zoom notifications, with upstream's method names.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <algorithm>
#include <vector>

#include "control/zoom/ZoomListener.h"

class ZoomControl {
public:
    double getZoom() const { return zoom; }
    /// Zoom relative to "100 %" (upstream: getZoom() / getZoom100Value()).
    double getZoomReal() const { return zoom / zoom100; }
    double getZoom100Value() const { return zoom100; }
    bool isZoomFitMode() const { return false; }
    bool isZoomPresentationMode() const { return false; }
    bool isZoomSequenceActive() const { return sequenceActive; }

    void addZoomListener(ZoomListener* l) { listeners.push_back(l); }
    void removeZoomListener(ZoomListener* l) { listeners.erase(std::remove(listeners.begin(), listeners.end(), l), listeners.end()); }

    // xournal-qt: fed by the canvas view
    void setZoom(double z, double z100) {
        if (z == zoom && z100 == zoom100) {
            return;
        }
        zoom = z;
        zoom100 = z100;
        for (ZoomListener* l: std::vector<ZoomListener*>(listeners)) {
            l->zoomChanged();
        }
    }
    void setZoomSequenceActive(bool active) { sequenceActive = active; }

private:
    std::vector<ZoomListener*> listeners;
    double zoom = 1.0;
    double zoom100 = 1.0;
    bool sequenceActive = false;
};
