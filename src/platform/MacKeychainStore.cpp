// SPDX-License-Identifier: GPL-3.0-or-later
#include "MacKeychainStore.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <QLoggingCategory>

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
    if (status == errSecItemNotFound) {
        return QString();
    }
    if (status != errSecSuccess || !data.get() || CFGetTypeID(data.get()) != CFDataGetTypeID()) {
        qCWarning(lcKeychain) << "Cannot read" << key << "from the Keychain, OSStatus" << status;
        return std::nullopt;
    }
    const auto bytes = static_cast<CFDataRef>(data.get());
    return QString::fromUtf8(reinterpret_cast<const char *>(CFDataGetBytePtr(bytes)),
                             static_cast<qsizetype>(CFDataGetLength(bytes)));
}

bool MacKeychainStore::remove(const QString &key)
{
    const CFRef<CFMutableDictionaryRef> query(newItemQuery(m_service, key));
    const OSStatus status = SecItemDelete(query.get());
    m_lastStatus = static_cast<int>(status);
    if (status != errSecSuccess && status != errSecItemNotFound) {
        qCWarning(lcKeychain) << "Cannot remove" << key << "from the Keychain, OSStatus" << status;
        return false;
    }
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
    if (!remove(key)) {
        return false;
    }
    const QByteArray utf8 = secret.toUtf8();
    const CFRef<CFDataRef> data(CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(utf8.constData()),
                                             static_cast<CFIndex>(utf8.size())));

    const CFRef<CFMutableDictionaryRef> query(newItemQuery(m_service, key));
    CFDictionarySetValue(query.get(), kSecValueData, data.get());
    CFDictionarySetValue(query.get(), kSecAttrLabel, CFSTR("Sugo"));
    const OSStatus status = SecItemAdd(query.get(), nullptr);
    m_lastStatus = static_cast<int>(status);
    if (status != errSecSuccess) {
        qCWarning(lcKeychain) << "Cannot store" << key << "in the Keychain, OSStatus" << status;
        return false;
    }
    return true;
}
