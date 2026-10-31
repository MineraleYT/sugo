// SPDX-License-Identifier: GPL-3.0-or-later
#include "Localization.h"

namespace Localization
{
QString translate(const char *context, const char *singular, const char *plural, qint64 count)
{
    Q_UNUSED(context)
    return QString::fromUtf8(plural && count != 1 ? plural : singular);
}

QString substitute(const QString &text, const QStringList &args)
{
    if (args.isEmpty()) {
        return text;
    }
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) == u'%' && i + 1 < text.size() && text.at(i + 1).isDigit() && text.at(i + 1) != u'0') {
            qsizetype end = i + 2;
            if (end < text.size() && text.at(end).isDigit()) {
                ++end;
            }
            const int index = QStringView(text).sliced(i + 1, end - i - 1).toInt();
            if (index >= 1 && index <= args.size()) {
                out += args.at(index - 1);
                i = end - 1;
                continue;
            }
        }
        out += text.at(i);
    }
    return out;
}
} // namespace Localization

// --- QML ---------------------------------------------------------------------

namespace
{
QStringList arguments(std::initializer_list<const QVariant *> values)
{
    QStringList list;
    for (const QVariant *value : values) {
        if (!value->isValid()) {
            break;
        }
        list.append(value->toString());
    }
    return list;
}
} // namespace

LocalizedContext::LocalizedContext(QObject *parent)
    : QObject(parent)
{
}

QString LocalizedContext::i18n(const QString &text, const QVariant &p1, const QVariant &p2, const QVariant &p3,
                               const QVariant &p4, const QVariant &p5, const QVariant &p6) const
{
    const QByteArray t = text.toUtf8();
    return Localization::substitute(Localization::translate(nullptr, t.constData()), arguments({&p1, &p2, &p3, &p4, &p5, &p6}));
}

QString LocalizedContext::i18nc(const QString &context, const QString &text, const QVariant &p1, const QVariant &p2,
                                const QVariant &p3, const QVariant &p4, const QVariant &p5, const QVariant &p6) const
{
    const QByteArray c = context.toUtf8();
    const QByteArray t = text.toUtf8();
    return Localization::substitute(Localization::translate(c.constData(), t.constData()), arguments({&p1, &p2, &p3, &p4, &p5, &p6}));
}

QString LocalizedContext::i18np(const QString &singular, const QString &plural, const QVariant &count, const QVariant &p2,
                                const QVariant &p3, const QVariant &p4, const QVariant &p5, const QVariant &p6) const
{
    const QByteArray s = singular.toUtf8();
    const QByteArray p = plural.toUtf8();
    const qint64 n = count.toLongLong();
    return Localization::substitute(Localization::translate(nullptr, s.constData(), p.constData(), n),
                                    arguments({&count, &p2, &p3, &p4, &p5, &p6}));
}

QString LocalizedContext::i18ncp(const QString &context, const QString &singular, const QString &plural, const QVariant &count,
                                 const QVariant &p2, const QVariant &p3, const QVariant &p4, const QVariant &p5,
                                 const QVariant &p6) const
{
    const QByteArray c = context.toUtf8();
    const QByteArray s = singular.toUtf8();
    const QByteArray p = plural.toUtf8();
    const qint64 n = count.toLongLong();
    return Localization::substitute(Localization::translate(c.constData(), s.constData(), p.constData(), n),
                                    arguments({&count, &p2, &p3, &p4, &p5, &p6}));
}
