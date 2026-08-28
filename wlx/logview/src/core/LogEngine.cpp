#include "LogEngine.h"

#include <re2/re2.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <ctime>

namespace {

const char *kReIso = R"((\d{4})-(\d{2})-(\d{2})[T ](\d{2}):(\d{2}):(\d{2}))";
const char *kReSyslog = R"((\w{3})\s+(\d{1,2})\s+(\d{2}):(\d{2}):(\d{2}))";
const char *kReNginx = R"((\d{2})/(\w{3})/(\d{4}):(\d{2}):(\d{2}):(\d{2}))";

int monthFromAbbr(const std::string &m)
{
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (int i = 0; i < 12; ++i)
        if (m == months[i]) return i + 1;
    return 1;
}

int currentYear()
{
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    return tmv.tm_year + 1900;
}

} // namespace

LogEngine::LogEngine() = default;

LogEngine::~LogEngine()
{
    stopSearch();
    cleanup();
}

void LogEngine::cleanup()
{
    if (m_mappedData && m_mappedData != MAP_FAILED) {
        munmap(const_cast<char *>(m_mappedData), m_mappedSize);
        m_mappedData = nullptr;
        m_mappedSize = 0;
    }
    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
    m_lineOffsets.clear();
    m_matches.clear();
    m_totalMatches = 0;
}

int LogEngine::lineCount() const
{
    return m_lineOffsets.size() > 1 ? static_cast<int>(m_lineOffsets.size() - 1) : 0;
}

std::string LogEngine::lineText(int row) const
{
    if (row < 0 || row >= lineCount() || !m_mappedData) return {};
    const uint64_t start = m_lineOffsets[row];
    const uint64_t end = m_lineOffsets[row + 1];
    uint64_t len = end - start;
    while (len > 0 && (m_mappedData[start + len - 1] == '\n' || m_mappedData[start + len - 1] == '\r'))
        --len;
    return std::string(m_mappedData + start, len);
}

void LogEngine::setHighlightRules(std::vector<EngineHighlightRule> rules)
{
    m_rules = std::move(rules);
}

void LogEngine::colorsForRow(int row, LogColor &background, LogColor &foreground) const
{
    background = LogColor{};
    foreground = LogColor{};

    // row == -1 is a documented return of log_tree_model_to_engine_row() for a
    // row outside the active filter, so it genuinely reaches here. The bounds
    // check below covers row < 0, but only after this line had already indexed
    // m_matches -- and "row < m_matches.size()" is true for -1, so m_matches[-1]
    // was read out of bounds before anything rejected it.
    if (row < 0) return;

    if (row < (int)m_matches.size() && m_matches[row]) {
        background = LogColor{60, 60, 0, true}; // dark yellow, matches original
        return;
    }

    if (!m_mappedData || m_rules.empty() || row < 0 || row + 1 >= (int)m_lineOffsets.size())
        return;

    const uint64_t start = m_lineOffsets[row];
    const uint64_t end = m_lineOffsets[row + 1];
    uint64_t len = end - start;
    while (len > 0 && (m_mappedData[start + len - 1] == '\n' || m_mappedData[start + len - 1] == '\r'))
        --len;
    re2::StringPiece linePiece(m_mappedData + start, len);

    for (const auto &rule : m_rules) {
        if (rule.compiledRegex && re2::RE2::PartialMatch(linePiece, *rule.compiledRegex)) {
            background = rule.backgroundColor;
            foreground = rule.foregroundColor;
            return;
        }
    }
}

bool LogEngine::loadFile(const std::string &filePath)
{
    stopSearch();
    cleanup();

    m_filePath = filePath;

    m_fd = open(filePath.c_str(), O_RDONLY);
    if (m_fd < 0) return false;

    struct stat st;
    if (fstat(m_fd, &st) != 0 || st.st_size == 0) {
        close(m_fd); m_fd = -1;
        return false;
    }

    m_mappedSize = static_cast<size_t>(st.st_size);
    m_mappedData = static_cast<const char *>(mmap(nullptr, m_mappedSize, PROT_READ, MAP_SHARED, m_fd, 0));
    if (m_mappedData == MAP_FAILED) {
        m_mappedData = nullptr; m_mappedSize = 0;
        close(m_fd); m_fd = -1;
        return false;
    }
    madvise(const_cast<char *>(m_mappedData), m_mappedSize, MADV_SEQUENTIAL);

    m_lineOffsets.reserve(m_mappedSize / 60);
    m_lineOffsets.push_back(0);
    for (size_t i = 0; i < m_mappedSize; ++i) {
        if (m_mappedData[i] == '\n' && i + 1 < m_mappedSize)
            m_lineOffsets.push_back(i + 1);
    }
    m_lineOffsets.push_back(m_mappedSize);

    buildTimestampIndex();
    return true;
}

void LogEngine::clearFile()
{
    if (m_filePath.empty()) return;
    int fd = open(m_filePath.c_str(), O_WRONLY | O_TRUNC);
    if (fd >= 0) close(fd);
    loadFile(m_filePath);
}

void LogEngine::deleteRows(const std::vector<int> &sourceRows)
{
    if (sourceRows.empty() || m_filePath.empty() || !m_mappedData) return;

    std::vector<bool> toDelete(lineCount(), false);
    for (int r : sourceRows)
        if (r >= 0 && r < lineCount()) toDelete[r] = true;

    std::string kept;
    kept.reserve(m_mappedSize);
    for (int i = 0; i < lineCount(); ++i) {
        if (toDelete[i]) continue;
        const uint64_t start = m_lineOffsets[i];
        const uint64_t end = m_lineOffsets[i + 1];
        kept.append(m_mappedData + start, end - start);
    }

    std::string path = m_filePath;
    cleanup();

    FILE *f = fopen(path.c_str(), "wb");
    if (f) {
        fwrite(kept.data(), 1, kept.size(), f);
        fclose(f);
    }

    loadFile(path);
}

void LogEngine::startSearch(const std::string &query, SearchFinishedCallback onFinished)
{
    stopSearch();

    if (query.empty() || lineCount() == 0) {
        m_matches.clear();
        m_totalMatches = 0;
        if (onFinished) onFinished(0);
        return;
    }

    m_matches.assign(lineCount(), false);
    m_totalMatches = 0;

    m_searchThread = std::jthread([this, pattern = query, onFinished](std::stop_token stoken) {
        re2::RE2 re(pattern);
        if (!re.ok()) {
            if (onFinished) onFinished(-1);
            return;
        }

        const int total = lineCount();
        int matches = 0;
        for (int i = 0; i < total; ++i) {
            if (stoken.stop_requested()) break;

            const uint64_t start = m_lineOffsets[i];
            const uint64_t end = m_lineOffsets[i + 1];
            uint64_t len = end - start;
            while (len > 0 && (m_mappedData[start + len - 1] == '\n' || m_mappedData[start + len - 1] == '\r'))
                --len;

            re2::StringPiece line(m_mappedData + start, len);
            if (re2::RE2::PartialMatch(line, re)) {
                m_matches[i] = true;
                ++matches;
            }
        }
        m_totalMatches = matches;
        if (onFinished) onFinished(matches);
    });
}

void LogEngine::stopSearch()
{
    if (m_searchThread.joinable()) {
        m_searchThread.request_stop();
        m_searchThread.join();
    }
}

bool LogEngine::isMatch(int row) const
{
    if (row < 0 || row >= (int)m_matches.size()) return false;
    return m_matches[row];
}

int LogEngine::matchCount() const { return m_totalMatches.load(); }

int LogEngine::nextMatch(int fromRow) const
{
    const int total = lineCount();
    if (total == 0 || m_matches.empty()) return -1;
    for (int i = 1; i <= total; ++i) {
        int idx = (fromRow + i) % total;
        if (m_matches[idx]) return idx;
    }
    return -1;
}

LogTimestamp LogEngine::tryParseTimestamp(const char *data, int len)
{
    re2::StringPiece sp(data, len);
    int y, mo, d, h, mi, s;
    std::string ms;

    if (re2::RE2::PartialMatch(sp, kReIso, &y, &mo, &d, &h, &mi, &s))
        return LogTimestamp{y, mo, d, h, mi, s, true};

    if (re2::RE2::PartialMatch(sp, kReNginx, &d, &ms, &y, &h, &mi, &s))
        return LogTimestamp{y, monthFromAbbr(ms), d, h, mi, s, true};

    if (re2::RE2::PartialMatch(sp, kReSyslog, &ms, &d, &h, &mi, &s))
        return LogTimestamp{currentYear(), monthFromAbbr(ms), d, h, mi, s, true};

    return LogTimestamp{};
}

LogTimestamp LogEngine::parseTimestampFromLine(std::string_view line)
{
    return tryParseTimestamp(line.data(), static_cast<int>(line.size()));
}

void LogEngine::buildTimestampIndex()
{
    m_interpolatedTimestamps.assign(lineCount(), LogTimestamp{});

    LogTimestamp lastKnown;
    LogTimestamp firstValid;

    for (int i = 0; i < lineCount(); ++i) {
        std::string line = lineText(i);
        LogTimestamp cur = parseTimestampFromLine(line);
        if (cur.valid) {
            lastKnown = cur;
            if (!firstValid.valid) firstValid = cur;
        }
        if (lastKnown.valid)
            m_interpolatedTimestamps[i] = lastKnown;
    }

    m_firstTimestamp = firstValid;
    m_lastTimestamp = lastKnown;
}

LogTimestamp LogEngine::getInterpolatedTimestamp(int row) const
{
    if (row < 0 || row >= (int)m_interpolatedTimestamps.size()) return LogTimestamp{};
    return m_interpolatedTimestamps[row];
}

LogEngine::TailResult LogEngine::refreshTail()
{
    TailResult result;
    result.oldLineCount = lineCount();

    struct stat st;
    if (stat(m_filePath.c_str(), &st) != 0) return result;
    size_t newSize = static_cast<size_t>(st.st_size);
    if (newSize <= m_mappedSize) return result;

    size_t oldSize = m_mappedSize;

    if (m_mappedData && m_mappedData != MAP_FAILED)
        munmap(const_cast<char *>(m_mappedData), m_mappedSize);

    m_mappedSize = newSize;
    m_mappedData = static_cast<const char *>(mmap(nullptr, m_mappedSize, PROT_READ, MAP_SHARED, m_fd, 0));
    if (m_mappedData == MAP_FAILED) {
        m_mappedData = nullptr; m_mappedSize = 0;
        return result;
    }

    // The old sentinel (== oldSize) is only a "resume point", not a real
    // row boundary, if the old file did NOT end with a newline (i.e. the
    // last row was still being written). If it DID end with a newline,
    // that sentinel is the genuine boundary between the old last row and
    // whatever comes next — dropping it unconditionally (as an earlier
    // version of this fix did) loses that boundary, since the scan below
    // only looks at bytes from oldSize onward and the newline that used to
    // terminate the old last row lives at oldSize - 1, outside that range.
    // Confirmed via a real append+refresh test: without this check, a
    // freshly appended newline-terminated line silently merged into the
    // previous row instead of becoming its own.
    bool oldEndedWithNewline = oldSize > 0 && m_mappedData[oldSize - 1] == '\n';
    if (!oldEndedWithNewline && !m_lineOffsets.empty())
        m_lineOffsets.pop_back();

    for (size_t i = oldSize; i < m_mappedSize; ++i) {
        if (m_mappedData[i] == '\n')
            m_lineOffsets.push_back(i + 1);
    }
    if (m_lineOffsets.empty() || m_lineOffsets.back() != m_mappedSize)
        m_lineOffsets.push_back(m_mappedSize);

    result.newLineCount = lineCount();
    result.grew = result.newLineCount > result.oldLineCount;
    return result;
}
