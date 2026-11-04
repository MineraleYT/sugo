// SPDX-License-Identifier: GPL-3.0-or-later
#include "MacPowerAssertion.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/pwr_mgt/IOPMLib.h>

#include <QLoggingCategory>

namespace
{
Q_LOGGING_CATEGORY(lcMacPower, "sugo.platform.power.mac")
}

static_assert(sizeof(IOPMAssertionID) == sizeof(std::uint32_t), "IOPMAssertionID is a uint32_t");

MacPowerAssertion::~MacPowerAssertion()
{
    release();
}

bool MacPowerAssertion::acquire()
{
    if (m_id != 0) {
        return true;
    }
    IOPMAssertionID id = kIOPMNullAssertionID;
    const IOReturn result = IOPMAssertionCreateWithName(kIOPMAssertPreventUserIdleDisplaySleep,
                                                        kIOPMAssertionLevelOn,
                                                        CFSTR("Sugo: Pomodoro work session in progress"),
                                                        &id);
    if (result != kIOReturnSuccess) {
        qCWarning(lcMacPower) << "IOPMAssertionCreateWithName failed:" << Qt::hex << result;
        return false;
    }
    m_id = id;
    qCDebug(lcMacPower) << "Display sleep prevented with assertion" << m_id;
    return true;
}

void MacPowerAssertion::release()
{
    if (m_id == 0) {
        return;
    }
    const IOReturn result = IOPMAssertionRelease(static_cast<IOPMAssertionID>(m_id));
    if (result != kIOReturnSuccess) {
        qCWarning(lcMacPower) << "IOPMAssertionRelease failed:" << Qt::hex << result;
    }
    qCDebug(lcMacPower) << "Display sleep assertion released" << m_id;
    m_id = 0;
}
