/*
 * Xournal++
 *
 * An element on the Document
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>  // for ptrdiff_t
#include <string>   // for string (xournal-qt: plugin data)
#include <cstdint>  // for uint32_t, int64_t (xournal-qt: group, creation time)
#include <memory>   // for unique_ptr
#include <vector>   // for vector

#include <gdk/gdk.h>  // for GdkRectangle

#include "util/Color.h"                     // for Color
#include "util/Rectangle.h"                 // for Rectangle
#include "util/serializing/Serializable.h"  // for Serializable

class ObjectInputStream;
class ObjectOutputStream;

namespace xoj::util {
template <class T>
struct Point;
};

enum ElementType { ELEMENT_STROKE = 1, ELEMENT_IMAGE, ELEMENT_TEXIMAGE, ELEMENT_TEXT, ELEMENT_LINK };

class ShapeContainer {
public:
    virtual bool contains(double x, double y) const = 0;

    virtual ~ShapeContainer() = default;
};

class Element;
using ElementPtr = std::unique_ptr<Element>;

class Element: public Serializable {
protected:
    Element(ElementType type);
    Element(const Element&) = default;
    Element& operator=(const Element&) = default;
    Element(Element&&) = default;
    Element& operator=(Element&&) = default;

public:
    ~Element() override = default;

    using Index = std::ptrdiff_t;
    static constexpr auto InvalidIndex = static_cast<Index>(-1);

public:
    ElementType getType() const;

    virtual const xoj::util::Point<double>& getOrigin() const = 0;

    virtual void move(double dx, double dy) = 0;
    virtual void scale(double x0, double y0, double fx, double fy, double rotation, bool restoreLineWidth) = 0;
    virtual void rotate(double x0, double y0, double th) = 0;

    void setColor(Color color);
    Color getColor() const;

    /// xournal-qt: the group the element belongs to in its layer (qt/groups; 0: none). Copied with the element, saved
    /// as the attribute `xqt-group`; not in serialize() (upstream's clipboard data stays upstream's).
    uint32_t getGroup() const { return group; }
    void setGroup(uint32_t g) { group = g; }

    /// xournal-qt: when the element was made (qt/timeline): milliseconds since 1970-01-01 UTC, 0: not known. Copied
    /// with the element, saved as the attribute `xqt-created`; not in serialize() (a pasted element is a new one).
    int64_t getCreated() const { return created; }
    void setCreated(int64_t ms) { created = ms; }

    /// xournal-qt: data a plugin keeps on the element (qt/docs/decisions/0008-js-plugins.md; a JSON object by plugin
    /// id, "": none). Copied with the element, saved as the attribute `xqt-data`; not in serialize().
    const std::string& getData() const { return data; }
    void setData(std::string d) { data = std::move(d); }

    const xoj::util::Rectangle<double>& getSnappedBounds() const;

    const xoj::util::Rectangle<double>& getBoundingBox() const;

    /// Returns true if the element's bounding box intersects the given rectangle, in Page coordinates
    bool intersectsArea(double x, double y, double width, double height) const;
    /// Returns the distance between the element "as drawn" and the point (x,y), in Page coordinates
    virtual double distanceTo(double x, double y) const = 0;
    bool hasBoundingBoxContaining(double x, double y) const;

    virtual bool isInSelection(ShapeContainer* container) const = 0;
    /**
     * Take 1:1 copy of this element
     */
    virtual auto clone() const -> ElementPtr = 0;

    void serialize(ObjectOutputStream& out) const override;
    void readSerialized(ObjectInputStream& in) override;

private:
protected:
    virtual void calcSize() const = 0;

protected:
    /// Whether the size has been calculated or not
    mutable bool sizeCalculated = false;

    /// Rectangular area containing the element as drawn
    mutable xoj::util::Rectangle<double> boundingBox{};

    /// The position and dimensions on the screen used for snapping
    mutable xoj::util::Rectangle<double> snappedBounds{};

private:
    /// Type of this element
    ElementType type;

    /// The color in RGB format
    Color color{0U};

    uint32_t group = 0;    ///< xournal-qt: see getGroup()
    int64_t created = 0;   ///< xournal-qt: see getCreated()
    std::string data;      ///< xournal-qt: see getData()
};

namespace xoj {

auto refElementContainer(const std::vector<ElementPtr>& elements) -> std::vector<Element*>;

}  // namespace xoj
