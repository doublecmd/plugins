#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <thread>
#include <atomic>
#include <memory>
#include <functional>

namespace re2 { class RE2; }

/// Toolkit-neutral log-file engine — extracted out of what used to be
/// LogModel (a QAbstractListModel subclass mixing mmap/RE2/threading logic
/// directly with Qt model plumbing). Owns the mmap, line-offset index,
/// background regex search, highlight-rule matching, and timestamp
/// interpolation; exposes plain callbacks instead of Qt signals. File
/// watching itself stays toolkit-side (QFileSystemWatcher for Qt, inotify
/// for GTK) — the toolkit layer calls refreshTail() when it's notified the
/// file grew, matching how LogModel::onFileChanged() drove the old code.
struct LogColor {
    int r = 0, g = 0, b = 0;
    bool valid = false;
};

struct LogTimestamp {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    bool valid = false;
};

struct EngineHighlightRule {
    std::string pattern;
    LogColor foregroundColor;
    LogColor backgroundColor;
    std::shared_ptr<re2::RE2> compiledRegex;
};

class LogEngine {
public:
    LogEngine();
    ~LogEngine();

    LogEngine(const LogEngine &) = delete;
    LogEngine &operator=(const LogEngine &) = delete;

    bool loadFile(const std::string &filePath);
    void clearFile();
    void deleteRows(const std::vector<int> &sourceRows);

    int lineCount() const;
    std::string lineText(int row) const;

    // Search — runs in a background jthread; onSearchFinished is invoked
    // from that thread (matchCount, or -1 for an invalid regex) — the
    // toolkit layer must marshal it back onto its own UI thread.
    using SearchFinishedCallback = std::function<void(int matchCount)>;
    void startSearch(const std::string &query, SearchFinishedCallback onFinished);
    void stopSearch();
    bool isMatch(int row) const;
    int matchCount() const;
    int nextMatch(int fromRow) const;

    // Highlighting
    void setHighlightRules(std::vector<EngineHighlightRule> rules);
    std::vector<EngineHighlightRule> highlightRules() const { return m_rules; }
    /// Returns {background, foreground} for a row: search-match background
    /// takes priority, then the first matching highlight rule, in order.
    void colorsForRow(int row, LogColor &background, LogColor &foreground) const;

    // Timestamps
    LogTimestamp firstTimestamp() const { return m_firstTimestamp; }
    LogTimestamp lastTimestamp() const { return m_lastTimestamp; }
    LogTimestamp getInterpolatedTimestamp(int row) const;
    static LogTimestamp parseTimestampFromLine(std::string_view line);

    // Follow / tail — call refreshTail() when the toolkit's file watcher
    // reports the file grew; returns {oldLineCount, newLineCount} so the
    // caller can emit the right "rows inserted" notification.
    void setFollowEnabled(bool enabled) { m_followEnabled = enabled; }
    bool followEnabled() const { return m_followEnabled; }
    struct TailResult { int oldLineCount = 0; int newLineCount = 0; bool grew = false; };
    TailResult refreshTail();

    const std::string &filePath() const { return m_filePath; }

private:
    void cleanup();
    void buildTimestampIndex();
    static LogTimestamp tryParseTimestamp(const char *data, int len);

    std::string m_filePath;

    const char *m_mappedData = nullptr;
    size_t m_mappedSize = 0;
    int m_fd = -1;

    std::vector<uint64_t> m_lineOffsets;

    std::vector<bool> m_matches;
    std::jthread m_searchThread;
    std::atomic<int> m_totalMatches{0};

    std::vector<LogTimestamp> m_interpolatedTimestamps;
    LogTimestamp m_firstTimestamp;
    LogTimestamp m_lastTimestamp;

    bool m_followEnabled = false;
    std::vector<EngineHighlightRule> m_rules;
};
