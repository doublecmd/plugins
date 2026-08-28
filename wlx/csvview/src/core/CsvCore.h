#pragma once

#include <string>
#include <vector>

/// Toolkit-neutral CSV/TSV tokenizing and serialization — extracted out of
/// the previous Qt-only plugin.cpp's `parse_line()` free function and the
/// inline escaping logic in `CsvViewerWidget::saveFile()`. Operates on
/// already-UTF-8 text; encoding conversion (enca/iconv) stays a separate
/// concern handled by the caller (Qt build keeps using EncodingUtils
/// unchanged; a from-scratch toolkit-neutral encoding layer is out of
/// scope for this pass — see the plugin's README/commit message).
namespace CsvCore {

struct Field {
    std::string text;
    bool wasQuoted = false;
};

/// Split one UTF-8 CSV/TSV line (already stripped of trailing \r\n) on
/// `separator`, honoring double-quoted fields that may themselves contain
/// the separator or embedded (doubled) quote characters — same algorithm
/// as the original parse_line(), just std::string instead of QStringList.
std::vector<Field> parseLine(const std::string &utf8Line, char separator);

/// Escape one field for output: doubles embedded quotes and wraps in
/// quotes if the field was originally quoted or contains the separator —
/// same rule as the original saveFile()'s inline logic.
std::string escapeField(const std::string &text, char separator, bool wasQuoted);

/// Join already-escaped fields with `separator`.
std::string joinRow(const std::vector<std::string> &escapedFields, char separator);

} // namespace CsvCore
