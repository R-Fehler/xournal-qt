/*
 * xournal-qt: the material of a page tile shown dark (qt/docs/features/dark-pages.md): the tile's texture looked up in
 * the dark table (canvas/DarkPages.h) on the GPU, except in the rectangles kept (pictures). No page is drawn again: the
 * tile is the page's picture as it is.
 *
 * Only with a graphics API (RHI); the software renderer ignores materials, and the canvas maps the tiles on the CPU
 * there instead (dark::apply). Built only when Qt Shader Tools were found (XQT_DARK_SHADER); else the CPU path
 * everywhere.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QRectF>
#include <QSGMaterial>
#include <QVector4D>
#include <array>

class QSGTexture;

class DarkTileMaterial final: public QSGMaterial {
public:
    static constexpr int MAX_KEEP = 8;

    DarkTileMaterial();
    QSGMaterialType* type() const override;
    QSGMaterialShader* createShader(QSGRendererInterface::RenderMode) const override;
    int compare(const QSGMaterial* other) const override;

    /// Whether the shader is there (the build had Qt Shader Tools)
    static bool available();

    QSGTexture* tile = nullptr;   ///< (the node's)
    QSGTexture* table = nullptr;  ///< (the canvas's)
    QVector4D balance{1, 1, 1, 1};
    std::array<QVector4D, MAX_KEEP> keep{};
    int keepCount = 0;
    /// Sets the kept rectangles (texture coordinates); false if they did not change
    bool setKeep(const std::vector<QRectF>& rects);
};
