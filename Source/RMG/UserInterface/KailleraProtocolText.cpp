#include "KailleraProtocolText.hpp"

#include <QChar>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <string>

namespace
{
constexpr unsigned int kJapaneseCodePage = 932;

QByteArray stripTrailingProtocolNulls(QByteArray bytes)
{
    const qsizetype nul = bytes.indexOf('\0');
    if (nul >= 0)
    {
        bytes.truncate(nul);
    }
    return bytes;
}

bool isUtf8Continuation(unsigned char value)
{
    return (value & 0xC0) == 0x80;
}

bool isValidUtf8(const QByteArray& bytes)
{
    qsizetype i = 0;
    const qsizetype size = bytes.size();
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.constData());

    while (i < size)
    {
        const unsigned char c = data[i];
        if (c <= 0x7F)
        {
            ++i;
            continue;
        }

        if (c >= 0xC2 && c <= 0xDF)
        {
            if (i + 1 >= size || !isUtf8Continuation(data[i + 1]))
            {
                return false;
            }
            i += 2;
            continue;
        }

        if (c == 0xE0)
        {
            if (i + 2 >= size || data[i + 1] < 0xA0 || data[i + 1] > 0xBF ||
                !isUtf8Continuation(data[i + 2]))
            {
                return false;
            }
            i += 3;
            continue;
        }

        if ((c >= 0xE1 && c <= 0xEC) || (c >= 0xEE && c <= 0xEF))
        {
            if (i + 2 >= size || !isUtf8Continuation(data[i + 1]) ||
                !isUtf8Continuation(data[i + 2]))
            {
                return false;
            }
            i += 3;
            continue;
        }

        if (c == 0xED)
        {
            if (i + 2 >= size || data[i + 1] < 0x80 || data[i + 1] > 0x9F ||
                !isUtf8Continuation(data[i + 2]))
            {
                return false;
            }
            i += 3;
            continue;
        }

        if (c == 0xF0)
        {
            if (i + 3 >= size || data[i + 1] < 0x90 || data[i + 1] > 0xBF ||
                !isUtf8Continuation(data[i + 2]) || !isUtf8Continuation(data[i + 3]))
            {
                return false;
            }
            i += 4;
            continue;
        }

        if (c >= 0xF1 && c <= 0xF3)
        {
            if (i + 3 >= size || !isUtf8Continuation(data[i + 1]) ||
                !isUtf8Continuation(data[i + 2]) || !isUtf8Continuation(data[i + 3]))
            {
                return false;
            }
            i += 4;
            continue;
        }

        if (c == 0xF4)
        {
            if (i + 3 >= size || data[i + 1] < 0x80 || data[i + 1] > 0x8F ||
                !isUtf8Continuation(data[i + 2]) || !isUtf8Continuation(data[i + 3]))
            {
                return false;
            }
            i += 4;
            continue;
        }

        return false;
    }

    return true;
}

#ifdef Q_OS_WIN
QString decodeWindowsCodePage(const QByteArray& bytes, UINT codePage)
{
    if (bytes.isEmpty())
    {
        return QString();
    }

    int chars = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, bytes.constData(),
                                    static_cast<int>(bytes.size()), nullptr, 0);
    if (chars <= 0)
    {
        chars = MultiByteToWideChar(codePage, 0, bytes.constData(),
                                    static_cast<int>(bytes.size()), nullptr, 0);
    }
    if (chars <= 0)
    {
        return QString();
    }

    std::wstring wide(static_cast<size_t>(chars), L'\0');
    chars = MultiByteToWideChar(codePage, 0, bytes.constData(),
                                static_cast<int>(bytes.size()), wide.data(), chars);
    if (chars <= 0)
    {
        return QString();
    }

    return QString::fromWCharArray(wide.data(), chars);
}

QByteArray encodeWindowsCodePage(const QString& text, UINT codePage, bool* lossless)
{
    if (lossless != nullptr)
    {
        *lossless = true;
    }
    if (text.isEmpty())
    {
        return QByteArray();
    }

    const std::wstring wide = text.toStdWString();
    BOOL usedDefaultChar = FALSE;
    int bytes = WideCharToMultiByte(codePage, WC_NO_BEST_FIT_CHARS, wide.data(),
                                    static_cast<int>(wide.size()), nullptr, 0,
                                    nullptr, &usedDefaultChar);
    if (bytes <= 0)
    {
        if (lossless != nullptr)
        {
            *lossless = false;
        }
        return QByteArray();
    }

    QByteArray encoded;
    encoded.resize(bytes);
    usedDefaultChar = FALSE;
    bytes = WideCharToMultiByte(codePage, WC_NO_BEST_FIT_CHARS, wide.data(),
                                static_cast<int>(wide.size()), encoded.data(), bytes,
                                nullptr, &usedDefaultChar);
    if (bytes <= 0 || usedDefaultChar)
    {
        if (lossless != nullptr)
        {
            *lossless = false;
        }
        return QByteArray();
    }

    encoded.truncate(bytes);
    return encoded;
}
#endif

QByteArray encodeProtocolTextUnlimited(const QString& text, bool* useUtf8)
{
    if (useUtf8 != nullptr)
    {
        *useUtf8 = true;
    }

#ifdef Q_OS_WIN
    bool lossless = false;
    QByteArray encoded = encodeWindowsCodePage(text, kJapaneseCodePage, &lossless);
    if (lossless)
    {
        if (useUtf8 != nullptr)
        {
            *useUtf8 = false;
        }
        return encoded;
    }
#endif

    return text.toUtf8();
}

qsizetype nextCodePointLength(const QString& text, qsizetype index)
{
    if (index + 1 < text.size() &&
        QChar::isHighSurrogate(text.at(index).unicode()) &&
        QChar::isLowSurrogate(text.at(index + 1).unicode()))
    {
        return 2;
    }
    return 1;
}
} // namespace

QString KailleraProtocolStringFromBytes(const char* text)
{
    return KailleraProtocolStringFromBytes(QByteArray(text ? text : ""));
}

QString KailleraProtocolStringFromBytes(const QByteArray& bytes)
{
    const QByteArray cleanBytes = stripTrailingProtocolNulls(bytes);
    if (cleanBytes.isEmpty())
    {
        return QString();
    }

    if (isValidUtf8(cleanBytes))
    {
        return QString::fromUtf8(cleanBytes);
    }

#ifdef Q_OS_WIN
    const QString japaneseText = decodeWindowsCodePage(cleanBytes, kJapaneseCodePage);
    if (!japaneseText.isEmpty())
    {
        return japaneseText;
    }
#endif

    return QString::fromLocal8Bit(cleanBytes);
}

QByteArray KailleraProtocolStringToBytes(const QString& text, qsizetype maxBytes)
{
    bool useUtf8 = true;
    const QByteArray encoded = encodeProtocolTextUnlimited(text, &useUtf8);
    if (maxBytes < 0 || encoded.size() <= maxBytes)
    {
        return encoded;
    }

    QByteArray truncated;
    for (qsizetype i = 0; i < text.size();)
    {
        const qsizetype codePointLength = nextCodePointLength(text, i);
        const QString segment = text.mid(i, codePointLength);
        const QByteArray segmentBytes = useUtf8
            ? segment.toUtf8()
#ifdef Q_OS_WIN
            : encodeWindowsCodePage(segment, kJapaneseCodePage, nullptr);
#else
            : segment.toUtf8();
#endif

        if (segmentBytes.isEmpty() || truncated.size() + segmentBytes.size() > maxBytes)
        {
            break;
        }

        truncated += segmentBytes;
        i += codePointLength;
    }

    return truncated;
}

QList<QByteArray> KailleraProtocolStringToChunks(const QString& text, qsizetype maxBytes)
{
    QList<QByteArray> chunks;
    if (maxBytes <= 0)
    {
        return chunks;
    }

    bool useUtf8 = true;
    encodeProtocolTextUnlimited(text, &useUtf8);

    QByteArray current;
    for (qsizetype i = 0; i < text.size();)
    {
        const qsizetype codePointLength = nextCodePointLength(text, i);
        const QString segment = text.mid(i, codePointLength);
        const QByteArray segmentBytes = useUtf8
            ? segment.toUtf8()
#ifdef Q_OS_WIN
            : encodeWindowsCodePage(segment, kJapaneseCodePage, nullptr);
#else
            : segment.toUtf8();
#endif

        if (segmentBytes.isEmpty())
        {
            i += codePointLength;
            continue;
        }

        if (!current.isEmpty() && current.size() + segmentBytes.size() > maxBytes)
        {
            chunks.append(current);
            current.clear();
        }

        if (segmentBytes.size() > maxBytes)
        {
            chunks.append(segmentBytes.left(maxBytes));
        }
        else
        {
            current += segmentBytes;
        }

        i += codePointLength;
    }

    if (!current.isEmpty())
    {
        chunks.append(current);
    }

    return chunks;
}
