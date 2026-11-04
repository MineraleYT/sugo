// SPDX-License-Identifier: GPL-3.0-or-later
#include "MacSessionEvents.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOMessage.h>
#include <IOKit/pwr_mgt/IOPMLib.h>

#include <QLoggingCategory>

#include <cstdint>
#include <utility>

namespace
{
Q_LOGGING_CATEGORY(lcMacSession, "sugo.platform.screenlock.mac")
const CFStringRef kScreenLocked = CFSTR("com.apple.screenIsLocked");
const CFStringRef kScreenUnlocked = CFSTR("com.apple.screenIsUnlocked");
} // namespace

static_assert(sizeof(io_connect_t) == sizeof(std::uint32_t), "io_connect_t is a 32-bit Mach port");
static_assert(sizeof(io_object_t) == sizeof(std::uint32_t), "io_object_t is a 32-bit Mach port");

struct MacSessionEvents::Callbacks {
    static void distributed(CFNotificationCenterRef, void *observer, CFNotificationName name, const void *, CFDictionaryRef)
    {
        auto *self = static_cast<MacSessionEvents *>(observer);
        if (!self || !name || !self->m_lockChanged) {
            return;
        }
        if (CFStringCompare(name, kScreenLocked, 0) == kCFCompareEqualTo) {
            self->m_lockChanged(true);
        } else if (CFStringCompare(name, kScreenUnlocked, 0) == kCFCompareEqualTo) {
            self->m_lockChanged(false);
        }
    }

    static void power(void *refCon, io_service_t, std::uint32_t messageType, void *messageArgument)
    {
        auto *self = static_cast<MacSessionEvents *>(refCon);
        if (!self) {
            return;
        }
        const auto notificationId = reinterpret_cast<std::intptr_t>(messageArgument);
        switch (messageType) {
        case kIOMessageCanSystemSleep:
            // Idle sleep may go ahead; nothing to prepare until it really happens.
            IOAllowPowerChange(static_cast<io_connect_t>(self->m_rootPort), notificationId);
            break;
        case kIOMessageSystemWillSleep:
            qCDebug(lcMacSession) << "The system is going to sleep";
            if (self->m_sleepChanged) {
                self->m_sleepChanged(true);
            }
            // Must always be answered, or the system waits 30 seconds before sleeping.
            IOAllowPowerChange(static_cast<io_connect_t>(self->m_rootPort), notificationId);
            break;
        case kIOMessageSystemHasPoweredOn:
            qCDebug(lcMacSession) << "The system woke up";
            if (self->m_sleepChanged) {
                self->m_sleepChanged(false);
            }
            break;
        default:
            break;
        }
    }
};

MacSessionEvents::MacSessionEvents(std::function<void(bool)> lockChanged, std::function<void(bool)> sleepChanged)
    : m_lockChanged(std::move(lockChanged))
    , m_sleepChanged(std::move(sleepChanged))
{
    CFNotificationCenterRef center = CFNotificationCenterGetDistributedCenter();
    CFNotificationCenterAddObserver(center, this, &Callbacks::distributed, kScreenLocked, nullptr,
                                    CFNotificationSuspensionBehaviorDeliverImmediately);
    CFNotificationCenterAddObserver(center, this, &Callbacks::distributed, kScreenUnlocked, nullptr,
                                    CFNotificationSuspensionBehaviorDeliverImmediately);

    IONotificationPortRef notifyPort = nullptr;
    io_object_t notifier = IO_OBJECT_NULL;
    const io_connect_t rootPort = IORegisterForSystemPower(this, &notifyPort, &Callbacks::power, &notifier);
    if (rootPort == MACH_PORT_NULL) {
        qCWarning(lcMacSession) << "IORegisterForSystemPower failed: sleep will not pause the timer";
        return;
    }
    m_rootPort = rootPort;
    m_notifyPort = notifyPort;
    m_notifier = notifier;
    CFRunLoopAddSource(CFRunLoopGetMain(), IONotificationPortGetRunLoopSource(notifyPort), kCFRunLoopCommonModes);
}

MacSessionEvents::~MacSessionEvents()
{
    CFNotificationCenterRemoveEveryObserver(CFNotificationCenterGetDistributedCenter(), this);

    if (m_rootPort != 0) {
        auto *notifyPort = static_cast<IONotificationPortRef>(m_notifyPort);
        CFRunLoopRemoveSource(CFRunLoopGetMain(), IONotificationPortGetRunLoopSource(notifyPort), kCFRunLoopCommonModes);
        io_object_t notifier = static_cast<io_object_t>(m_notifier);
        IODeregisterForSystemPower(&notifier);
        IOServiceClose(static_cast<io_connect_t>(m_rootPort));
        IONotificationPortDestroy(notifyPort);
    }
}
