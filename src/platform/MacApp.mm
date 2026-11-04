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

void macSetDockBadge(const QString &label)
{
    // NSApp is nil when Qt runs without the cocoa platform (offscreen): messages to nil do nothing.
    NSString *text = label.isEmpty() ? nil : label.toNSString();
    NSApp.dockTile.badgeLabel = text;
}

void macSetDockIconHidden(bool hidden)
{
    const NSApplicationActivationPolicy policy = hidden ? NSApplicationActivationPolicyAccessory : NSApplicationActivationPolicyRegular;
    if (NSApp && NSApp.activationPolicy != policy) {
        [NSApp setActivationPolicy:policy];
    }
}
