#include <wlxbase_wlqt/EncodingUtils.h>

#include <QFile>
#include <locale.h>
#include <string.h>
#include <glib.h>
#include <enca.h>

namespace QtWlPlugin {

QString EncodingUtils::detectEncoding(const QByteArray &data, const QString &language)
{
    if (data.isEmpty())
        return {};

    QString lang = language;
    if (lang.isEmpty()) {
        // Derive from current locale without changing it
        const char *loc = setlocale(LC_ALL, nullptr);
        if (loc && strlen(loc) >= 2)
            lang = QString::fromLatin1(loc, 2);
        else
            lang = "__";
    }

    EncaAnalyser analyser = enca_analyser_alloc(lang.toStdString().c_str());
    if (!analyser)
        return {};

    enca_set_threshold(analyser, 1.38);
    enca_set_multibyte(analyser, 1);
    enca_set_ambiguity(analyser, 1);
    enca_set_garbage_test(analyser, 1);
    enca_set_filtering(analyser, 0);

    EncaEncoding encoding = enca_analyse(analyser,
        (unsigned char *)data.data(), (size_t)data.size());

    QString result;
    if (encoding.charset > 0 && encoding.charset != 27) {
        const char *name = enca_charset_name(encoding.charset, ENCA_NAME_STYLE_ICONV);
        if (name)
            result = QString::fromLatin1(name);
    }

    enca_analyser_free(analyser);
    return result;
}

QString EncodingUtils::detectFileEncoding(const QString &filePath, const QString &language,
                                           int sampleSize, bool readAll)
{
    QFile file(filePath);
    if (!file.open(QFile::ReadOnly))
        return {};

    QByteArray data = readAll ? file.readAll() : file.read(sampleSize);
    file.close();

    return detectEncoding(data, language);
}

QByteArray EncodingUtils::toUtf8(const QByteArray &data, const QString &fromEncoding)
{
    if (fromEncoding.isEmpty() || fromEncoding.compare("UTF-8", Qt::CaseInsensitive) == 0)
        return data;

    gsize len;
    gchar *converted = g_convert_with_fallback(
        data.data(), data.size(),
        "UTF-8", fromEncoding.toStdString().c_str(),
        NULL, NULL, &len, NULL);

    if (converted) {
        QByteArray result(converted, len);
        g_free(converted);
        return result;
    }
    return data;
}

QByteArray EncodingUtils::fromUtf8(const QString &text, const QString &toEncoding)
{
    if (toEncoding.isEmpty() || toEncoding.compare("UTF-8", Qt::CaseInsensitive) == 0)
        return text.toUtf8();

    QByteArray utf8 = text.toUtf8();
    gsize len;
    gchar *converted = g_convert_with_fallback(
        utf8.data(), utf8.size(),
        toEncoding.toStdString().c_str(), "UTF-8",
        NULL, NULL, &len, NULL);

    if (converted) {
        QByteArray result(converted, len);
        g_free(converted);
        return result;
    }
    return utf8;
}

QString EncodingUtils::decodeToString(const QByteArray &data, const QString &language)
{
    QString encoding = detectEncoding(data, language);
    if (encoding.isEmpty())
        return QString::fromUtf8(data);

    QByteArray utf8 = toUtf8(data, encoding);
    return QString::fromUtf8(utf8);
}

bool EncodingUtils::isEncaAvailable()
{
    EncaAnalyser analyser = enca_analyser_alloc("__");
    if (analyser) {
        enca_analyser_free(analyser);
        return true;
    }
    return false;
}

} // namespace QtWlPlugin
