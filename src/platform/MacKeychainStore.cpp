// SPDX-License-Identifier: GPL-3.0-or-later
#include "MacKeychainStore.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <QLoggingCategory>

#include <sodium.h>

#include <cstring>
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

/// The secret as UTF-8, in bytes that are wiped when they go.
SecretBytes secretUtf8(const QString &secret)
{
    QByteArray utf8 = secret.toUtf8();
    SecretBytes bytes = SecretBytes::fromRaw(utf8);
    if (!utf8.isEmpty()) {
        sodium_memzero(utf8.data(), size_t(utf8.size()));
    }
    return bytes;
}

/// The item's data: a copy of \a secret that is zeroed before it is released, as the item was
/// added (or failed to be) by then. Null when out of memory.
class SecretData
{
public:
    explicit SecretData(const SecretBytes &secret)
        : m_data(CFDataCreateMutable(kCFAllocatorDefault, 0))
    {
        if (m_data) {
            CFDataAppendBytes(m_data, secret.data(), static_cast<CFIndex>(secret.size()));
        }
    }
    ~SecretData()
    {
        if (m_data) {
            if (const CFIndex length = CFDataGetLength(m_data); length > 0) {
                sodium_memzero(CFDataGetMutableBytePtr(m_data), size_t(length));
            }
            CFRelease(m_data);
        }
    }
    SecretData(const SecretData &) = delete;
    SecretData &operator=(const SecretData &) = delete;
    CFDataRef get() const { return m_data; }

private:
    CFMutableDataRef m_data;
};
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
        m_known.insert_or_assign(key, SecretBytes());
        return QString();
    }
    if (status != errSecSuccess || !data.get() || CFGetTypeID(data.get()) != CFDataGetTypeID()) {
        qCWarning(lcKeychain) << "Cannot read" << key << "from the Keychain, OSStatus" << status;
        m_known.erase(key);
        return std::nullopt;
    }
    const auto bytes = static_cast<CFDataRef>(data.get());
    const auto length = static_cast<qsizetype>(CFDataGetLength(bytes));
    SecretBytes known(length);
    if (length > 0) {
        std::memcpy(known.data(), CFDataGetBytePtr(bytes), size_t(length));
    }
    m_known.insert_or_assign(key, std::move(known));
    return QString::fromUtf8(reinterpret_cast<const char *>(CFDataGetBytePtr(bytes)), length);
}

bool MacKeychainStore::remove(const QString &key)
{
    const CFRef<CFMutableDictionaryRef> query(newItemQuery(m_service, key));
    const OSStatus status = SecItemDelete(query.get());
    m_lastStatus = static_cast<int>(status);
    if (status != errSecSuccess && status != errSecItemNotFound) {
        qCWarning(lcKeychain) << "Cannot remove" << key << "from the Keychain, OSStatus" << status;
        m_known.erase(key);
        return false;
    }
    m_known.insert_or_assign(key, SecretBytes());
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
    SecretBytes utf8 = secretUtf8(secret);
    const SecretData data(utf8);
    if (!data.get()) {
        // Checked before anything is deleted: a null value in the query would crash, not fail.
        qCWarning(lcKeychain) << "Cannot store" << key << "in the Keychain: out of memory";
        m_lastStatus = static_cast<int>(errSecAllocate);
        return false;
    }
    // What the item holds, if known without asking the user (not read again here: reading an item
    // another build wrote makes macOS ask, even after the user said "Deny"), to put back below.
    // Taken out of m_known: wiped when this returns, unless restore() puts it back there.
    std::optional<SecretBytes> previous;
    if (auto known = m_known.find(key); known != m_known.end()) {
        previous = std::move(known->second);
        m_known.erase(known);
    }
    if (!remove(key)) {
        return false;
    }
    const OSStatus status = m_failNextAdd != 0 ? static_cast<OSStatus>(std::exchange(m_failNextAdd, 0))
                                               : addItem(key, data.get());
    m_lastStatus = static_cast<int>(status);
    if (status != errSecSuccess) {
        qCWarning(lcKeychain) << "Cannot store" << key << "in the Keychain, OSStatus" << status;
        restore(key, std::move(previous));
        return false;
    }
    m_known.insert_or_assign(key, std::move(utf8));
    return true;
}

OSStatus MacKeychainStore::addItem(const QString &key, CFDataRef data) const
{
    const CFRef<CFMutableDictionaryRef> query(newItemQuery(m_service, key));
    CFDictionarySetValue(query.get(), kSecValueData, data);
    CFDictionarySetValue(query.get(), kSecAttrLabel, CFSTR("Sugo"));
    return SecItemAdd(query.get(), nullptr);
}

void MacKeychainStore::restore(const QString &key, std::optional<SecretBytes> previous)
{
    // What the failed add left is not known for sure (errSecDuplicateItem: someone else's item).
    m_known.erase(key);
    // Unknown (never read, or the user refused): gone with the failed write, as before.
    // Known to be absent: nothing to put back.
    if (!previous || previous->isEmpty()) {
        return;
    }
    const SecretData data(*previous);
    // errSecDuplicateItem: someone else stored the item meanwhile; theirs stays.
    const OSStatus status = data.get() ? addItem(key, data.get()) : errSecAllocate;
    if (status == errSecSuccess) {
        m_known.insert_or_assign(key, std::move(*previous));
    } else {
        qCWarning(lcKeychain) << "Cannot put the previous" << key << "back in the Keychain, OSStatus" << status;
    }
}
