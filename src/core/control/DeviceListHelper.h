/*
 * Xournal++
 *
 * Helper functions to iterate over devices
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>  // for string, basic_string
#include <vector>  // for vector

#include <gdk/gdk.h>  // for GdkInputSource, GdkDevice

#include "gui/inputdevices/InputEvents.h"  // for InputDeviceClass

class Settings;

class InputDevice {
public:
    InputDevice();
#ifndef XOJ_NO_GTK  // xournal-qt
    explicit InputDevice(GdkDevice* device);
#endif
    explicit InputDevice(std::string name, GdkInputSource source);
    ~InputDevice() = default;

public:
    std::string getType() const;
    std::string getName() const;
    GdkInputSource getSource() const;
    void updateType(GdkInputSource newSource);

    bool operator==(const InputDevice& inputDevice) const;

private:
    std::string name;
    GdkInputSource source{GDK_SOURCE_MOUSE};
};

#ifndef XOJ_NO_GTK  // xournal-qt: GDK seat based device enumeration
namespace DeviceListHelper {
std::vector<InputDevice> getDeviceList(Settings* settings, bool ignoreTouchDevices = false);
InputDeviceClass getSourceMapping(GdkInputSource source, Settings* settings);
}  // namespace DeviceListHelper
#endif
