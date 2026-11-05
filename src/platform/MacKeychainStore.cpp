// SPDX-License-Identifier: GPL-3.0-or-later
#include "MacKeychainStore.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <QLoggingCategory>

#include <utility>

namespace
{
Q_LOGGING_CATEGORY(lcKeychain, "sugo.platform.keychain")

/// Owns one Core Foundation reference.
template<typename T>
class CFRef
{
public:
    explicit CFRef(T ref = nullptr)
        : m_ref(ref)
    {
    }
    ~CFRef()
    {
        if (m_ref) {
            CFRelease(m_ref);
        }
    }
    CFRef(const CFRef &) = delete;
    CFRef &operator=(const CFRef &) = delete;
    T get() const { return m_ref; }

private:
    T m_ref;
};

/// The query that names one item: generic password, service, account.
CFMutableDictionaryRef newItemQuery(const QString &service, const QString &account)
{
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                             &kCFTypeDictionaryValueCallBacks);
    const CFRef<CFStringRef> serviceString(service.toCFString());
    const CFRef<CFStringRef> accountString(account.toCFString());
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, serviceString.get());
    CFDictionarySetValue(query, kSecAttrAccount, accountString.get());
    return query;
}

/// The secret as the item's data, UTF-8; null when out of memory.
CFDataRef newSecretData(const QString &secret)
{
    const QByteArray utf8 = secret.toUtf8();
    return CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(utf8.constData()),
                        static_cast<CFIndex>(utf8.size()));
}
} // namespace

MacKeychainStore::MacKeychainStore(const QString &service)
    : m_service(service)
{
}

std::optional<QString> MacKeychainStore::read(const QString &key)
{
    const CFRef<CFMutableDictionaryRef> query(newItemQuery(m_service, key));
    CFDictionarySetValue(query.get(), kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query.get(), kSecMatchLimit, kSecMatchLimitOne);

    CFTypeRef result = nullptr;
    const OSStatus status = SecItemCopyMatching(query.get(), &result);
    const CFRef<CFTypeRef> data(result);
    m_lastStatus = static_cast<int>(status);
    // Not errSecInteractionNotAllowed: nobody was asked (no login session), worth trying again.
    m_lastReadRefused = status == errSecAuthFailed || status == errSecUserCanceled;
    if (status == errSecItemNotFound) {
        m_known.insert(key, QString());
        return QString();
    }
    if (status != errSecSuccess || !data.get() || CFGetTypeID(data.get()) != CFDataGetTypeID()) {
        qCWarning(lcKeychain) << "Cannot read" << key << "from the Keychain, OSStatus" << status;
        m_known.remove(key);
        return std::nullopt;
    }
    const auto bytes = static_cast<CFDataRef>(data.get());
    const QString secret = QString::fromUtf8(reinterpret_cast<const char *>(CFDataGetBytePtr(bytes)),
                                             static_cast<qsizetype>(CFDataGetLength(bytes)));
    m_known.insert(key, secret);
    return secret;
}

bool MacKeychainStore::remove(const QString &key)
{
    const CFRef<CFMutableDictionaryRef> query(newItemQuery(m_service, key));
    const OSStatus status = SecItemDelete(query.get());
    m_lastStatus = static_cast<int>(status);
    if (status != errSecSuccess && status != errSecItemNotFound) {
        qCWarning(lcKeychain) << "Cannot remove" << key << "from the Keychain, OSStatus" << status;
        m_known.remove(key);
        return false;
    }
    m_known.insert(key, QString());
    return true;
}

bool MacKeychainStore::write(const QString &key, const QString &secret)
{
    if (secret.isEmpty()) {
        return remove(key);
    }
    // Replaced, not updated: the item's access list trusts the build that created it, and an ad
    // hoc signed build (each update has another code hash) may still change the data of an item
    // it cannot read. SecItemUpdate then succeeds, but leaves an item that neither build reads
    // without asking the user. Deleting needs no permission, and the new item is this build's.
    const CFRef<CFDataRef> data(newSecretData(secret));
    if (!data.get()) {
        // Checked before anything is deleted: a null value in the query would crash, not fail.
        qCWarning(lcKeychain) << "Cannot store" << key << "in the Keychain: out of memory";
        m_lastStatus = static_cast<int>(errSecAllocate);
        return false;
    }
    // What the item holds, if known without asking the user (not read again here: reading an item
    // another build wrote makes macOS ask, even after the user said "Deny"), to put back below.
    const auto known = m_known.constFind(key);
    const std::optional<QString> previous = known != m_known.cend() ? std::optional<QString>(*known) : std::nullopt;
    if (!remove(key)) {
        return false;
    }
    const OSStatus status = m_failNextAdd != 0 ? static_cast<OSStatus>(std::exchange(m_failNextAdd, 0))
                                               : addItem(key, data.get());
    m_lastStatus = static_cast<int>(status);
    if (status != errSecSuccess) {
        qCWarning(lcKeychain) << "Cannot store" << key << "in the Keychain, OSStatus" << status;
        restore(key, previous);
        return false;
    }
    m_known.insert(key, secret);
    return true;
}

OSStatus MacKeychainStore::addItem(const QString &key, CFDataRef data) const
{
    const CFRef<CFMutableDictionaryRef> query(newItemQuery(m_service, key));
    CFDictionarySetValue(query.get(), kSecValueData, data);
    CFDictionarySetValue(query.get(), kSecAttrLabel, CFSTR("Sugo"));
    return SecItemAdd(query.get(), nullptr);
}

void MacKeychainStore::restore(const QString &key, const std::optional<QString> &previous)
{
    // What the failed add left is not known for sure (errSecDuplicateItem: someone else's item).
    m_known.remove(key);
    // Unknown (never read, or the user refused): gone with the failed write, as before.
    // Known to be absent: nothing to put back.
    if (!previous || previous->isEmpty()) {
        return;
    }
    const CFRef<CFDataRef> data(newSecretData(*previous));
    // errSecDuplicateItem: someone else stored the item meanwhile; theirs stays.
    const OSStatus status = data.get() ? addItem(key, data.get()) : errSecAllocate;
    if (status == errSecSuccess) {
        m_known.insert(key, *previous);
    } else {
        qCWarning(lcKeychain) << "Cannot put the previous" << key << "back in the Keychain, OSStatus" << status;
    }
}
