// SPDX-License-Identifier: GPL-3.0-or-later
#include "MacNotifier.h"

#include "MacNotificationMapping.h"

#include <QHash>
#include <QLoggingCategory>
#include <QPointer>
#include <QSet>

#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

Q_LOGGING_CATEGORY(lcMacNotifier, "sugo.platform.notifier.macos")

namespace SugoMacNotifierDetail
{
/// What the framework's blocks may reach. They run on the framework's own queues and only hand
/// work to the main queue, where `notifier` (cleared by ~MacNotifier) is read: never elsewhere.
struct Shared {
    MacNotifier::Private *notifier = nullptr;
};
} // namespace SugoMacNotifierDetail

using SugoMacNotifierDetail::Shared;

@interface SugoNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
- (instancetype)initWithShared:(std::shared_ptr<Shared>)shared;
@end

struct MacNotifier::Private {
    std::unique_ptr<Notifier> fallback;
    MacNotificationRegistry registry;
    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
    SugoNotificationDelegate *delegate = nil;
    /// Categories registered so far: setNotificationCategories: replaces the whole set.
    NSMutableSet<UNNotificationCategory *> *categories = nil;
    QSet<QString> categoryIds;

    /// Notifications waiting for the permission to be read, by ticket.
    struct Pending {
        QPointer<QObject> context;
        Notification notification;
    };
    QHash<quint64, Pending> pending;
    quint64 nextTicket = 1;

    void deliver(quint64 ticket, bool allowed);
    void post(QObject *context, const Notification &notification);
};

namespace
{
UNNotificationCategory *makeCategory(const QString &identifier, const QList<MacNotificationMapping::ActionSpec> &specs)
{
    NSMutableArray<UNNotificationAction *> *actions = [NSMutableArray array];
    for (const MacNotificationMapping::ActionSpec &spec : specs) {
        if (spec.textInput) {
            [actions addObject:[UNTextInputNotificationAction actionWithIdentifier:spec.identifier.toNSString()
                                                                             title:spec.title.toNSString()
                                                                           options:UNNotificationActionOptionNone
                                                              textInputButtonTitle:spec.submitTitle.toNSString()
                                                              textInputPlaceholder:spec.placeholder.toNSString()]];
        } else {
            [actions addObject:[UNNotificationAction actionWithIdentifier:spec.identifier.toNSString()
                                                                    title:spec.title.toNSString()
                                                                  options:UNNotificationActionOptionNone]];
        }
    }
    return [UNNotificationCategory categoryWithIdentifier:identifier.toNSString()
                                                  actions:actions
                                        intentIdentifiers:@[]
                                                  // A dismissed banner reaches the delegate too,
                                                  // which then forgets its handlers.
                                                  options:UNNotificationCategoryOptionCustomDismissAction];
}
} // namespace

@implementation SugoNotificationDelegate {
    std::shared_ptr<Shared> _shared;
}

- (instancetype)initWithShared:(std::shared_ptr<Shared>)shared
{
    self = [super init];
    if (self) {
        _shared = std::move(shared);
    }
    return self;
}

// Shown while Sugo is the active application too (by default macOS would drop them).
- (void)userNotificationCenter:(UNUserNotificationCenter *)center
       willPresentNotification:(UNNotification *)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions options))completionHandler
{
    (void)center;
    (void)notification;
    completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList);
}

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
    didReceiveNotificationResponse:(UNNotificationResponse *)response
             withCompletionHandler:(void (^)(void))completionHandler
{
    (void)center;
    const QString requestId = QString::fromNSString(response.notification.request.identifier);
    NSString *action = response.actionIdentifier;
    QString actionId;
    if ([action isEqualToString:UNNotificationDefaultActionIdentifier]) {
        actionId = MacNotificationMapping::defaultActionId();
    } else if ([action isEqualToString:UNNotificationDismissActionIdentifier]) {
        actionId = MacNotificationMapping::dismissActionId();
    } else {
        actionId = QString::fromNSString(action);
    }
    QString text;
    if ([response isKindOfClass:[UNTextInputNotificationResponse class]]) {
        text = QString::fromNSString(((UNTextInputNotificationResponse *)response).userText);
    }
    const std::shared_ptr<Shared> shared = _shared;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (shared->notifier) {
            shared->notifier->registry.respond(requestId, actionId, text);
        }
        completionHandler();
    });
}
@end

bool MacNotifier::isSupported()
{
    // UNUserNotificationCenter raises an exception in a process without a bundle identifier
    // (a bare binary, the tests).
    NSString *identifier = NSBundle.mainBundle.bundleIdentifier;
    return identifier != nil && identifier.length > 0;
}

MacNotifier::MacNotifier(std::unique_ptr<Notifier> fallback, QObject *parent)
    : Notifier(parent)
    , d(std::make_unique<Private>())
{
    d->fallback = std::move(fallback);
    d->shared->notifier = d.get();
    d->categories = [NSMutableSet set];
    d->delegate = [[SugoNotificationDelegate alloc] initWithShared:d->shared];

    UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];
    // The center keeps only a weak reference: d->delegate holds it.
    center.delegate = d->delegate;
    // No sound: Sugo plays its own. The first call shows the system's question; later ones
    // return what the user chose.
    [center requestAuthorizationWithOptions:UNAuthorizationOptionAlert
                          completionHandler:^(BOOL granted, NSError *error) {
                              if (error) {
                                  qCWarning(lcMacNotifier) << "Cannot ask for notifications:"
                                                           << QString::fromNSString(error.localizedDescription);
                              } else if (!granted) {
                                  qCInfo(lcMacNotifier) << "Notifications are not allowed; using the menu bar icon";
                              }
                          }];
}

MacNotifier::~MacNotifier()
{
    d->shared->notifier = nullptr;
    UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];
    if (center.delegate == d->delegate) {
        center.delegate = nil;
    }
}

void MacNotifier::notify(QObject *context, const Notification &notification)
{
    const quint64 ticket = d->nextTicket++;
    d->pending.insert(ticket, {context, notification});
    const std::shared_ptr<Shared> shared = d->shared;
    // Read every time: the user may allow or forbid notifications in System Settings at any time.
    [[UNUserNotificationCenter currentNotificationCenter] getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings *settings) {
        const UNAuthorizationStatus status = settings.authorizationStatus;
        dispatch_async(dispatch_get_main_queue(), ^{
            MacNotifier::Private *notifier = shared->notifier;
            if (!notifier) {
                return;
            }
            if (status == UNAuthorizationStatusAuthorized || status == UNAuthorizationStatusProvisional) {
                notifier->deliver(ticket, true);
            } else if (status == UNAuthorizationStatusNotDetermined) {
                // The question at start-up has not been answered yet: ask (again) and wait.
                [[UNUserNotificationCenter currentNotificationCenter]
                    requestAuthorizationWithOptions:UNAuthorizationOptionAlert
                                  completionHandler:^(BOOL granted, NSError *) {
                                      dispatch_async(dispatch_get_main_queue(), ^{
                                          if (shared->notifier) {
                                              shared->notifier->deliver(ticket, granted);
                                          }
                                      });
                                  }];
            } else {
                notifier->deliver(ticket, false);
            }
        });
    }];
}

void MacNotifier::Private::deliver(quint64 ticket, bool allowed)
{
    const Pending item = pending.take(ticket);
    if (!item.context) {
        return;
    }
    if (allowed) {
        post(item.context, item.notification);
    } else if (fallback) {
        fallback->notify(item.context, item.notification);
    }
}

void MacNotifier::Private::post(QObject *context, const Notification &notification)
{
    const QList<MacNotificationMapping::ActionSpec> specs = MacNotificationMapping::actionsFor(notification);
    const QString category = MacNotificationMapping::categoryIdentifier(specs);
    UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];
    // The earlier banners of the same sender (an older phase, an older session) are replaced, as
    // they would have expired on Linux: their handlers are forgotten, so an answer still given
    // in one (a task note for a session long past) reaches nothing, and they leave the
    // Notification Center.
    const QStringList replaced = registry.takeEarlier(context);
    if (!replaced.isEmpty()) {
        NSMutableArray<NSString *> *identifiers = [NSMutableArray arrayWithCapacity:NSUInteger(replaced.size())];
        for (const QString &id : replaced) {
            [identifiers addObject:id.toNSString()];
        }
        [center removeDeliveredNotificationsWithIdentifiers:identifiers];
        [center removePendingNotificationRequestsWithIdentifiers:identifiers];
    }
    const QString requestId = registry.add(context, notification);

    UNMutableNotificationContent *content = [[UNMutableNotificationContent alloc] init];
    content.title = notification.title.toNSString();
    content.body = notification.text.toNSString();
    // content.sound stays nil: Sugo plays its own sound.
    if (!category.isEmpty()) {
        content.categoryIdentifier = category.toNSString();
    }
    UNNotificationRequest *request = [UNNotificationRequest requestWithIdentifier:requestId.toNSString()
                                                                          content:content
                                                                          trigger:nil];
    const std::shared_ptr<Shared> sharedState = shared;
    void (^add)(void) = ^{
        [center addNotificationRequest:request
                 withCompletionHandler:^(NSError *error) {
                     if (!error) {
                         return;
                     }
                     const QString message = QString::fromNSString(error.localizedDescription);
                     dispatch_async(dispatch_get_main_queue(), ^{
                         MacNotifier::Private *notifier = sharedState->notifier;
                         if (!notifier) {
                             return;
                         }
                         // Allowed a moment ago, refused now (switched off in between, or the
                         // system's notification service failed): the menu bar icon shows it.
                         qCWarning(lcMacNotifier) << "Cannot show a notification; using the menu bar icon:" << message;
                         const std::optional<MacNotificationRegistry::Unshown> unshown = notifier->registry.takeUnshown(requestId);
                         if (unshown && notifier->fallback) {
                             notifier->fallback->notify(unshown->context, unshown->notification);
                         }
                     });
                 }];
    };

    if (category.isEmpty() || categoryIds.contains(category)) {
        add();
        return;
    }
    categoryIds.insert(category);
    [categories addObject:makeCategory(category, specs)];
    [center setNotificationCategories:[categories copy]];
    // The categories are registered asynchronously; reading them back waits for that, so the
    // first notification of a new set of buttons already shows them.
    [center getNotificationCategoriesWithCompletionHandler:^(NSSet<UNNotificationCategory *> *registered) {
        (void)registered;
        add();
    }];
}
