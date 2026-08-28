#pragma once

#include <QString>
#include <QByteArray>

namespace QtWlPlugin {

/// Encoding detection and conversion utility for plugin developers.
///
/// Wraps the enca library for detection and glib for encoding conversion.
/// All methods are static — no instance needed.
class EncodingUtils {
public:
    /// Detect the encoding of raw byte data.
    /// @param data Raw bytes to analyze
    /// @param language 2-letter ISO language code hint for enca (e.g. "ru", "en", "de").
    ///                 If empty, derived from the current locale.
    /// @return Detected encoding name (e.g. "UTF-8", "windows-1251") or empty on failure.
    static QString detectEncoding(const QByteArray &data, const QString &language = {});

    /// Detect encoding from a file. Reads up to sampleSize bytes by default.
    static QString detectFileEncoding(const QString &filePath, const QString &language = {},
                                      int sampleSize = 4096, bool readAll = false);

    /// Convert byte data from one encoding to UTF-8.
    /// Returns the original data if conversion fails.
    static QByteArray toUtf8(const QByteArray &data, const QString &fromEncoding);

    /// Convert a UTF-8 QString to a target encoding.
    /// Returns UTF-8 bytes if conversion fails.
    static QByteArray fromUtf8(const QString &text, const QString &toEncoding);

    /// Detect encoding and decode to QString in one call.
    static QString decodeToString(const QByteArray &data, const QString &language = {});

    /// Check if enca support is available at runtime.
    static bool isEncaAvailable();
};

} // namespace QtWlPlugin
