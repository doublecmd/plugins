#include "CsvCore.h"

#include <algorithm>
#include <cctype>

namespace {

std::vector<std::string> splitOn(const std::string &text, char separator)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t pos = text.find(separator, start);
        if (pos == std::string::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

std::string trimmed(const std::string &s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

void replaceAll(std::string &s, const std::string &from, const std::string &to)
{
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

int countChar(const std::string &s, char c)
{
    return static_cast<int>(std::count(s.begin(), s.end(), c));
}

} // namespace

namespace CsvCore {

std::vector<Field> parseLine(const std::string &utf8LineIn, char separator)
{
    std::string text = utf8LineIn;
    if (text.size() >= 2 && text[text.size() - 2] == '\r' && text[text.size() - 1] == '\n')
        text.erase(text.size() - 2);
    else if (!text.empty() && text.back() == '\n')
        text.pop_back();

    std::vector<std::string> rawlist = splitOn(text, separator);
    std::vector<Field> list;

    for (size_t c = 0; c < rawlist.size(); ++c) {
        Field field;
        const std::string &cur = rawlist[c];

        if (!cur.empty() && cur.front() == '"' && cur.back() != '"') {
            std::string temp = cur;
            if (c < rawlist.size() - 1) {
                for (size_t x = c + 1; x < rawlist.size(); ++x) {
                    const std::string &nitm = rawlist[x];
                    if (!nitm.empty() && nitm.back() == '"') {
                        // Join rawlist[c..x] with separator, then strip the
                        // outer leading/trailing quote characters.
                        std::string joined;
                        for (size_t k = c; k <= x; ++k) {
                            if (k > c) joined += separator;
                            joined += rawlist[k];
                        }
                        if (joined.size() >= 2)
                            joined = joined.substr(1, joined.size() - 2);
                        if (countChar(joined, '"') % 2 == 0) {
                            temp = joined;
                            c = x;
                            break;
                        }
                    }
                }
            }
            field.text = temp;
            field.wasQuoted = true;
        } else {
            std::string val = trimmed(cur);
            bool quoted = val.size() >= 2 && val.front() == '"' && val.back() == '"';
            if (quoted)
                val = val.substr(1, val.size() - 2);
            field.text = val;
            field.wasQuoted = quoted;
        }

        replaceAll(field.text, "\"\"", "\"");
        list.push_back(std::move(field));
    }

    return list;
}

std::string escapeField(const std::string &text, char separator, bool wasQuoted)
{
    bool needsQuote = wasQuoted || text.find(separator) != std::string::npos;
    if (!needsQuote) return text;

    std::string out = text;
    replaceAll(out, "\"", "\"\"");
    return "\"" + out + "\"";
}

std::string joinRow(const std::vector<std::string> &escapedFields, char separator)
{
    std::string out;
    for (size_t i = 0; i < escapedFields.size(); ++i) {
        if (i > 0) out += separator;
        out += escapedFields[i];
    }
    return out;
}

} // namespace CsvCore
