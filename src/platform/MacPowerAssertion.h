// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

/**
 * macOS only: one IOKit power assertion that keeps the display (and so the system) from idle
 * sleep, the backend of PowerInhibitor on macOS. Unlike the D-Bus path it answers at once.
 *
 * Plain C++ over IOKit/pwr_mgt/IOPMLib.h; the assertion is released on destruction.
 */
class MacPowerAssertion
{
public:
    MacPowerAssertion() = default;
    ~MacPowerAssertion();
    MacPowerAssertion(const MacPowerAssertion &) = delete;
    MacPowerAssertion &operator=(const MacPowerAssertion &) = delete;

    /// Takes the assertion if not already held. False if IOKit refused it.
    bool acquire();
    /// Gives the assertion back; no-op if none is held.
    void release();
    bool isHeld() const { return m_id != 0; }

private:
    std::uint32_t m_id = 0; // IOPMAssertionID; 0 (kIOPMNullAssertionID) when none is held
};
