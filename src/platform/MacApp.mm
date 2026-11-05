// SPDX-License-Identifier: GPL-3.0-or-later
#include "MacApp.h"

#import <AppKit/AppKit.h>

#include <QGuiApplication>
#include <QWindow>

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

QuitPolicy::Origin macCurrentQuitOrigin()
{
    NSAppleEventDescriptor *event = [[NSAppleEventManager sharedAppleEventManager] currentAppleEvent];
    if (!event || event.eventClass != kCoreEventClass || event.eventID != kAEQuitApplication) {
        return QuitPolicy::Origin::Application;
    }
    // The login window gives the reason as an attribute; look at the parameters as well.
    NSAppleEventDescriptor *reason = [event attributeDescriptorForKeyword:kAEQuitReason];
    if (!reason) {
        reason = [event paramDescriptorForKeyword:kAEQuitReason];
    }
    return reason && reason.typeCodeValue != 0 ? QuitPolicy::Origin::SessionEnding : QuitPolicy::Origin::External;
}

void macHideWindowTitle(QWindow *window)
{
    if (!window || QGuiApplication::platformName() != QLatin1String("cocoa")) {
        return;
    }
    // With the cocoa platform the native handle is the window's NSView.
    auto *view = (__bridge NSView *)reinterpret_cast<void *>(window->winId());
    view.window.titleVisibility = NSWindowTitleHidden;
}
