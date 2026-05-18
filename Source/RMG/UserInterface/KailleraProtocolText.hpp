#ifndef KAILLERAPROTOCOLTEXT_HPP
#define KAILLERAPROTOCOLTEXT_HPP

#include <QByteArray>
#include <QList>
#include <QString>

QString KailleraProtocolStringFromBytes(const char* text);
QString KailleraProtocolStringFromBytes(const QByteArray& bytes);

QByteArray KailleraProtocolStringToBytes(const QString& text, qsizetype maxBytes = -1);
QList<QByteArray> KailleraProtocolStringToChunks(const QString& text, qsizetype maxBytes);

#endif // KAILLERAPROTOCOLTEXT_HPP
