/*
 * xournal-qt: GTK-free implementation of upstream gui/inputdevices/DeviceId.h.
 *
 * In the Qt build the opaque `GdkDevice*` identity is the address of the QPointingDevice (reinterpret_cast by the Qt
 * input layer). Upstream treats trackpoint and touchpad as one device; Qt reports both as the same core pointer
 * anyway, so that special case is not needed.
 *
 * @license GNU GPLv2 or later
 */
#include "gui/inputdevices/DeviceId.h"

DeviceId::DeviceId(const GdkDevice* id): id(id) {}
void DeviceId::reset(const GdkDevice* id) { *this = DeviceId(id); }
DeviceId::operator bool() const { return id != nullptr; }
bool DeviceId::operator==(const DeviceId& o) const { return (id == o.id) || (trackpointOrTouchpad && o.trackpointOrTouchpad); }
bool DeviceId::operator!=(const DeviceId& o) const { return !(*this == o); }
