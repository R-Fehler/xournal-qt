/*
 * xournal-qt: the material of a page tile shown dark (see DarkTileMaterial.h).
 *
 * @license GNU GPLv2 or later
 */
#include "DarkTileMaterial.h"

#include <QSGMaterialShader>
#include <QSGTexture>
#include <cstring>

namespace {

class DarkTileShader final: public QSGMaterialShader {
public:
    DarkTileShader() {
        setShaderFileName(VertexStage, QStringLiteral(":/xqt-shaders/darktile.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/xqt-shaders/darktile.frag.qsb"));
    }

    bool updateUniformData(RenderState& state, QSGMaterial* newMaterial, QSGMaterial*) override {
        // std140: mat4 (64), float opacity (4), float keepCount (4), padding to 16 (8), vec4 balance, vec4 keep[8]
        QByteArray* buf = state.uniformData();
        Q_ASSERT(buf->size() >= 64 + 16 + 16 + 16 * DarkTileMaterial::MAX_KEEP);
        if (state.isMatrixDirty()) {
            const QMatrix4x4 m = state.combinedMatrix();
            std::memcpy(buf->data(), m.constData(), 64);
        }
        if (state.isOpacityDirty()) {
            const float opacity = state.opacity();
            std::memcpy(buf->data() + 64, &opacity, 4);
        }
        const auto* mat = static_cast<DarkTileMaterial*>(newMaterial);
        const float count = static_cast<float>(mat->keepCount);
        std::memcpy(buf->data() + 68, &count, 4);
        const float balance[4] = {mat->balance.x(), mat->balance.y(), mat->balance.z(), 1};
        std::memcpy(buf->data() + 80, balance, 16);
        for (int i = 0; i < DarkTileMaterial::MAX_KEEP; ++i) {
            const QVector4D& k = mat->keep[static_cast<size_t>(i)];
            const float v[4] = {k.x(), k.y(), k.z(), k.w()};
            std::memcpy(buf->data() + 96 + 16 * i, v, 16);
        }
        return true;
    }

    void updateSampledImage(RenderState& state, int binding, QSGTexture** texture, QSGMaterial* newMaterial,
                            QSGMaterial*) override {
        auto* mat = static_cast<DarkTileMaterial*>(newMaterial);
        QSGTexture* t = binding == 1 ? mat->tile : mat->table;
        if (t) {
            t->setFiltering(QSGTexture::Linear);
            t->setHorizontalWrapMode(QSGTexture::ClampToEdge);
            t->setVerticalWrapMode(QSGTexture::ClampToEdge);
            t->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());  // (uploaded when first used)
        }
        *texture = t;
    }
};

}  // namespace

DarkTileMaterial::DarkTileMaterial() { setFlag(QSGMaterial::Blending, false); }

QSGMaterialType* DarkTileMaterial::type() const {
    static QSGMaterialType t;
    return &t;
}

QSGMaterialShader* DarkTileMaterial::createShader(QSGRendererInterface::RenderMode) const { return new DarkTileShader; }

int DarkTileMaterial::compare(const QSGMaterial* other) const {
    const auto* o = static_cast<const DarkTileMaterial*>(other);
    if (tile != o->tile) {
        return tile < o->tile ? -1 : 1;
    }
    if (table != o->table) {
        return table < o->table ? -1 : 1;
    }
    if (balance != o->balance || keepCount != o->keepCount || keep != o->keep) {
        return this < o ? -1 : 1;
    }
    return 0;
}

bool DarkTileMaterial::available() {
#ifdef XQT_DARK_SHADER
    return true;
#else
    return false;
#endif
}

bool DarkTileMaterial::setKeep(const std::vector<QRectF>& rects) {
    std::array<QVector4D, MAX_KEEP> k{};
    int n = 0;
    for (const QRectF& r: rects) {
        if (n == MAX_KEEP) {
            break;
        }
        k[static_cast<size_t>(n++)] = QVector4D(static_cast<float>(r.left()), static_cast<float>(r.top()),
                                                static_cast<float>(r.right()), static_cast<float>(r.bottom()));
    }
    if (n == keepCount && k == keep) {
        return false;
    }
    keep = k;
    keepCount = n;
    return true;
}
