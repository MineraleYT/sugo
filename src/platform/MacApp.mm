// SPDX-License-Identifier: GPL-3.0-or-later
#include "MacApp.h"

#import <AppKit/AppKit.h>

void macActivateApp()
{
    if (@available(macOS 14.0, *)) {
        // Cooperative activation: macOS may decline it when the user is busy in another application.
        [NSApp activate];
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
    }
}
