// SPDX-License-Identifier: GPL-3.0-or-later
// macOS only: the IOKit power assertion behind PowerInhibitor.
#include <QCoreApplication>
#include <QTest>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <unistd.h>

#include "MacPowerAssertion.h"
#include "PowerInhibitor.h"

namespace
{
/// How many "prevent display sleep" assertions this process holds, according to IOKit.
int displaySleepAssertionsOfThisProcess()
{
    CFDictionaryRef byProcess = nullptr;
    if (IOPMCopyAssertionsByProcess(&byProcess) != kIOReturnSuccess || !byProcess) {
        return -1;
    }
    int count = 0;
    const int pid = static_cast<int>(getpid());
    CFNumberRef pidNumber = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pid);
    const auto assertions = static_cast<CFArrayRef>(CFDictionaryGetValue(byProcess, pidNumber));
    if (assertions && CFGetTypeID(assertions) == CFArrayGetTypeID()) {
        for (CFIndex i = 0; i < CFArrayGetCount(assertions); ++i) {
            const auto assertion = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(assertions, i));
            const auto type = static_cast<CFStringRef>(CFDictionaryGetValue(assertion, kIOPMAssertionTypeKey));
            if (type && CFStringCompare(type, kIOPMAssertPreventUserIdleDisplaySleep, 0) == kCFCompareEqualTo) {
                ++count;
            }
        }
    }
    CFRelease(pidNumber);
    CFRelease(byProcess);
    return count;
}
} // namespace

class MacPowerAssertionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void acquireAndReleaseShowInIOKit()
    {
        const int before = displaySleepAssertionsOfThisProcess();
        QVERIFY(before >= 0);
        {
            MacPowerAssertion assertion;
            QVERIFY(assertion.acquire());
            QVERIFY(assertion.isHeld());
            QVERIFY(assertion.acquire()); // taking it twice holds one assertion
            QCOMPARE(displaySleepAssertionsOfThisProcess(), before + 1);
            assertion.release();
            QVERIFY(!assertion.isHeld());
            QCOMPARE(displaySleepAssertionsOfThisProcess(), before);
            QVERIFY(assertion.acquire());
        }
        // The destructor gives it back.
        QCOMPARE(displaySleepAssertionsOfThisProcess(), before);
    }

    void powerInhibitorUsesTheAssertion()
    {
        const int before = displaySleepAssertionsOfThisProcess();
        PowerInhibitor inhibitor;
        inhibitor.inhibit();
        QCOMPARE(displaySleepAssertionsOfThisProcess(), before + 1);
        inhibitor.release();
        QCOMPARE(displaySleepAssertionsOfThisProcess(), before);
        inhibitor.inhibit();
        inhibitor.shutdown();
        QCOMPARE(displaySleepAssertionsOfThisProcess(), before);
    }
};

QTEST_GUILESS_MAIN(MacPowerAssertionTest)
#include "tst_macpowerassertion.moc"
