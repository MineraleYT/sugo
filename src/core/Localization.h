// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <type_traits>

/**
 * The texts of the program, in English only for now. i18n(), i18nc() (with a context),
 * i18np() and i18ncp() (singular and plural) mark them, so translations can come back later
 * without touching the sources (po/Messages.sh still extracts them).
 *
 * Placeholders %1..%99 are filled in one pass (an argument containing "%2" is not filled
 * again). In the plural forms the count is %1.
 */
namespace Localization
{
/// `context` and `plural` may be null. With a plural, `count` picks the form (English rule).
QString translate(const char *context, const char *singular, const char *plural = nullptr, qint64 count = 0);

/// Fills %1..%n from `args` (index 0 is %1).
QString substitute(const QString &text, const QStringList &args);

template<typename T>
QString toArgument(const T &value)
{
    if constexpr (std::is_same_v<T, bool>) {
        return value ? QStringLiteral("true") : QStringLiteral("false");
    } else if constexpr (std::is_arithmetic_v<T>) {
        return QString::number(value);
    } else if constexpr (std::is_convertible_v<T, const char *>) {
        return QString::fromUtf8(value);
    } else {
        return QString(value);
    }
}
} // namespace Localization

template<typename... Args>
QString i18n(const char *text, const Args &...args)
{
    return Localization::substitute(Localization::translate(nullptr, text), {Localization::toArgument(args)...});
}

template<typename... Args>
QString i18nc(const char *context, const char *text, const Args &...args)
{
    return Localization::substitute(Localization::translate(context, text), {Localization::toArgument(args)...});
}

template<typename... Args>
QString i18np(const char *singular, const char *plural, qint64 count, const Args &...args)
{
    return Localization::substitute(Localization::translate(nullptr, singular, plural, count),
                                    {QString::number(count), Localization::toArgument(args)...});
}

template<typename... Args>
QString i18ncp(const char *context, const char *singular, const char *plural, qint64 count, const Args &...args)
{
    return Localization::substitute(Localization::translate(context, singular, plural, count),
                                    {QString::number(count), Localization::toArgument(args)...});
}

/**
 * The i18n functions of the QML files, as the engine's root context object.
 */
class LocalizedContext : public QObject
{
    Q_OBJECT
public:
    explicit LocalizedContext(QObject *parent = nullptr);

    Q_INVOKABLE QString i18n(const QString &text, const QVariant &p1 = {}, const QVariant &p2 = {}, const QVariant &p3 = {},
                             const QVariant &p4 = {}, const QVariant &p5 = {}, const QVariant &p6 = {}) const;
    Q_INVOKABLE QString i18nc(const QString &context, const QString &text, const QVariant &p1 = {}, const QVariant &p2 = {},
                              const QVariant &p3 = {}, const QVariant &p4 = {}, const QVariant &p5 = {}, const QVariant &p6 = {}) const;
    Q_INVOKABLE QString i18np(const QString &singular, const QString &plural, const QVariant &count, const QVariant &p2 = {},
                              const QVariant &p3 = {}, const QVariant &p4 = {}, const QVariant &p5 = {}, const QVariant &p6 = {}) const;
    Q_INVOKABLE QString i18ncp(const QString &context, const QString &singular, const QString &plural, const QVariant &count,
                               const QVariant &p2 = {}, const QVariant &p3 = {}, const QVariant &p4 = {}, const QVariant &p5 = {},
                               const QVariant &p6 = {}) const;
};
