#include "DiagramRenderer.h"

#include <dlfcn.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <poll.h>
#include <signal.h>
#include <cstring>
#include <chrono>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <cstdio>
#include <cctype>

extern char **environ;

namespace {

// ── Minimal subprocess runner (replaces QProcess) ──────────────────────
// Captures stdout only; timeout is enforced via poll() on the read end,
// which covers the real failure mode we care about (a hung renderer),
// same as QProcess::waitForFinished(timeoutMs) did.

struct ProcessResult {
    bool started = false;
    int exitCode = -1;
    std::string stdoutData;
};

ProcessResult runProcess(const std::string &exe, const std::vector<std::string> &args,
                          const std::string *stdinData, int timeoutMs)
{
    ProcessResult result;

    int outPipe[2];
    if (pipe(outPipe) != 0) return result;

    int inPipe[2] = {-1, -1};
    if (stdinData && pipe(inPipe) != 0) {
        close(outPipe[0]); close(outPipe[1]);
        return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, outPipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, outPipe[0]);
    posix_spawn_file_actions_addclose(&actions, outPipe[1]);
    if (stdinData) {
        posix_spawn_file_actions_adddup2(&actions, inPipe[0], STDIN_FILENO);
        posix_spawn_file_actions_addclose(&actions, inPipe[0]);
        posix_spawn_file_actions_addclose(&actions, inPipe[1]);
    }

    std::vector<char *> argv;
    argv.push_back(const_cast<char *>(exe.c_str()));
    for (auto &a : args) argv.push_back(const_cast<char *>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid = -1;
    int rc = posix_spawnp(&pid, exe.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);

    close(outPipe[1]);
    if (stdinData) close(inPipe[0]);

    if (rc != 0) {
        close(outPipe[0]);
        if (stdinData) close(inPipe[1]);
        return result;
    }
    result.started = true;

    if (stdinData) {
        size_t off = 0;
        while (off < stdinData->size()) {
            ssize_t n = write(inPipe[1], stdinData->data() + off, stdinData->size() - off);
            if (n <= 0) break;
            off += static_cast<size_t>(n);
        }
        close(inPipe[1]);
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    bool timedOut = false;
    char buf[8192];
    while (true) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                              deadline - std::chrono::steady_clock::now())
                              .count();
        if (remaining <= 0) { timedOut = true; break; }

        struct pollfd pfd { outPipe[0], POLLIN, 0 };
        int pr = poll(&pfd, 1, static_cast<int>(remaining));
        if (pr < 0) break;
        if (pr == 0) { timedOut = true; break; }

        if (pfd.revents & (POLLIN | POLLHUP)) {
            ssize_t n = read(outPipe[0], buf, sizeof(buf));
            if (n > 0)
                result.stdoutData.append(buf, static_cast<size_t>(n));
            else
                break; // EOF or error
        } else {
            break;
        }
    }
    close(outPipe[0]);

    if (timedOut)
        kill(pid, SIGKILL);

    int status = 0;
    waitpid(pid, &status, 0);
    if (!timedOut && WIFEXITED(status))
        result.exitCode = WEXITSTATUS(status);
    return result;
}

bool fileExists(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

std::string readFile(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool writeFile(const std::string &path, const std::string &data)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    return f.good();
}

std::string toLower(std::string s)
{
    for (auto &c : s) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string extensionOf(const std::string &path)
{
    auto slash = path.find_last_of('/');
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return {};
    return toLower(path.substr(dot + 1));
}

std::string dirOf(const std::string &path)
{
    auto slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string base64Encode(const std::string &data)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < data.size()) {
        unsigned v = (static_cast<unsigned char>(data[i]) << 16)
                   | (static_cast<unsigned char>(data[i + 1]) << 8)
                   | static_cast<unsigned char>(data[i + 2]);
        out += table[(v >> 18) & 0x3F];
        out += table[(v >> 12) & 0x3F];
        out += table[(v >> 6) & 0x3F];
        out += table[v & 0x3F];
        i += 3;
    }
    size_t rem = data.size() - i;
    if (rem == 1) {
        unsigned v = static_cast<unsigned char>(data[i]) << 16;
        out += table[(v >> 18) & 0x3F];
        out += table[(v >> 12) & 0x3F];
        out += "==";
    } else if (rem == 2) {
        unsigned v = (static_cast<unsigned char>(data[i]) << 16) | (static_cast<unsigned char>(data[i + 1]) << 8);
        out += table[(v >> 18) & 0x3F];
        out += table[(v >> 12) & 0x3F];
        out += table[(v >> 6) & 0x3F];
        out += '=';
    }
    return out;
}

std::string percentEncode(const std::string &data)
{
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(data.size());
    for (unsigned char c : data) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[(c >> 4) & 0xF];
            out += hex[c & 0xF];
        }
    }
    return out;
}

std::string hexEncode(const std::string &data)
{
    static const char *hex = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (unsigned char c : data) {
        out += hex[(c >> 4) & 0xF];
        out += hex[c & 0xF];
    }
    return out;
}

std::string trim(const std::string &s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// std::stod throws std::out_of_range for a value beyond double's range and
// std::invalid_argument if given something it can't parse. This is reached
// from ListLoad -- an extern "C" boundary called directly by DC's Pascal
// runtime -- and a C++ exception unwinding across that boundary is undefined
// behavior, not a clean plugin-only failure.
double safeStod(const std::string &s, double fallback = 0.0)
{
    try {
        return std::stod(s);
    } catch (const std::exception &) {
        return fallback;
    }
}

} // namespace

namespace DiagramRenderer {

std::string pluginDir()
{
    Dl_info info;
    if (dladdr(reinterpret_cast<void *>(&pluginDir), &info) != 0 && info.dli_fname) {
        std::string path(info.dli_fname);
        auto slash = path.find_last_of('/');
        if (slash != std::string::npos)
            return path.substr(0, slash);
    }
    return {};
}

// ── Settings: minimal "[section]\nkey=value" INI, replacing QSettings ──

void Settings::loadOrInitDefaults(const std::string &iniPath, const std::string &pluginName)
{
    std::ifstream f(iniPath);
    std::map<std::string, std::string> values;
    if (f) {
        std::string line, section;
        while (std::getline(f, line)) {
            line = trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            if (line.front() == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
                continue;
            }
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            values[section + "/" + trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
        }
    }

    auto getBool = [&](const std::string &key, bool def) {
        auto it = values.find(pluginName + "/" + key);
        if (it == values.end()) return def;
        return it->second == "true" || it->second == "1";
    };
    auto getStr = [&](const std::string &key, const std::string &def) {
        auto it = values.find(pluginName + "/" + key);
        return it == values.end() ? def : it->second;
    };
    auto getInt = [&](const std::string &key, int def) {
        auto it = values.find(pluginName + "/" + key);
        if (it == values.end()) return def;
        try { return std::stoi(it->second); } catch (...) { return def; }
    };

    autoReloadEnabled = getBool("auto_reload", autoReloadEnabled);
    darkMode = getBool("dark_mode", darkMode);
    useSystemDarkMode = getBool("use_system_dark_mode", useSystemDarkMode);
    renderer = getStr("renderer", renderer);
    mermaidRenderer = getStr("mermaid_renderer", mermaidRenderer);
    plantumlPath = getStr("plantuml_path", plantumlPath);
    javaPath = getStr("java_path", javaPath);
    plantumlServerUrl = getStr("plantuml_server_url", plantumlServerUrl);
    mmdcPath = getStr("mmdc_path", mmdcPath);
    mermaidServerUrl = getStr("mermaid_server_url", mermaidServerUrl);
    timeoutMs = getInt("timeout_ms", timeoutMs);

    save(iniPath, pluginName);
}

void Settings::save(const std::string &iniPath, const std::string &pluginName) const
{
    std::ofstream f(iniPath, std::ios::trunc);
    if (!f) return;
    f << "[" << pluginName << "]\n";
    f << "auto_reload=" << (autoReloadEnabled ? "true" : "false") << "\n";
    f << "dark_mode=" << (darkMode ? "true" : "false") << "\n";
    f << "use_system_dark_mode=" << (useSystemDarkMode ? "true" : "false") << "\n";
    f << "renderer=" << renderer << "\n";
    f << "mermaid_renderer=" << mermaidRenderer << "\n";
    f << "plantuml_path=" << plantumlPath << "\n";
    f << "java_path=" << javaPath << "\n";
    f << "plantuml_server_url=" << plantumlServerUrl << "\n";
    f << "mmdc_path=" << mmdcPath << "\n";
    f << "mermaid_server_url=" << mermaidServerUrl << "\n";
    f << "timeout_ms=" << timeoutMs << "\n";
}

// ── Mermaid ──────────────────────────────────────────────────────────

namespace {

std::string runMermaidWeb(const Settings &s, const std::string &inputPath, bool darkMode)
{
    std::string code = readFile(inputPath);
    if (code.empty()) return {};

    // mermaid.ink defaults to its light "default" theme regardless of our
    // own dark background -- confirmed via a direct request that arrows and
    // edge labels render with dark strokes (#333333/#552222) unless a
    // `?theme=dark` query param is added, which switches them to light
    // strokes (#ccc/#ddd) that stay legible on our dark canvas.
    std::string url = s.mermaidServerUrl + "/svg/" + percentEncode(base64Encode(code)) +
                       (darkMode ? "?theme=dark" : "");

    ProcessResult r = runProcess("curl",
        {"-s", "-f", "--max-time", std::to_string(s.timeoutMs / 1000), url},
        nullptr, s.timeoutMs);
    if (r.started && r.exitCode == 0 && !r.stdoutData.empty())
        return r.stdoutData;
    return {};
}

std::string runMermaidLocal(const Settings &s, const std::string &inputPath, bool darkMode)
{
    std::string tempConfigPath = "/tmp/diagramview_mermaid_" + std::to_string(getpid()) + ".json";
    writeFile(tempConfigPath, R"({
  "htmlLabels": false,
  "flowchart": { "htmlLabels": false },
  "sequence": { "htmlLabels": false },
  "gantt": { "htmlLabels": false },
  "journey": { "htmlLabels": false },
  "class": { "htmlLabels": false },
  "state": { "htmlLabels": false },
  "er": { "htmlLabels": false },
  "pie": { "htmlLabels": false },
  "c4": { "htmlLabels": false }
})");

    std::string outputPath = "/tmp/diagramview_mermaid_" + std::to_string(getpid()) + ".svg";

    auto cleanup = [&]() {
        remove(tempConfigPath.c_str());
        remove(outputPath.c_str());
    };

    auto buildArgs = [&](bool withNpxPrefix) {
        std::vector<std::string> args;
        if (withNpxPrefix) { args.push_back("-y"); args.push_back("@mermaid-js/mermaid-cli"); }
        args.push_back("-i"); args.push_back(inputPath);
        args.push_back("-o"); args.push_back(outputPath);
        args.push_back("-e"); args.push_back("svg");
        if (darkMode) { args.push_back("--theme"); args.push_back("dark"); }
        if (fileExists(tempConfigPath)) { args.push_back("-c"); args.push_back(tempConfigPath); }
        return args;
    };

    auto tryExe = [&](const std::string &exe) -> std::string {
        ProcessResult r = runProcess(exe, buildArgs(false), nullptr, s.timeoutMs);
        if (r.started && r.exitCode == 0 && fileExists(outputPath)) {
            std::string svg = readFile(outputPath);
            cleanup();
            return svg;
        }
        return {};
    };

    // 1. configured mmdc_path  2. DC config dir  3. plugin dir  4. plain "mmdc"
    if (auto svg = tryExe(s.mmdcPath); !svg.empty()) return svg;

    std::string configDir = dirOf(s.mmdcPath); // best-effort; real config dir passed by caller via Settings if needed
    std::string pluginMmdc = pluginDir().empty() ? std::string() : pluginDir() + "/mmdc";
    if (!pluginMmdc.empty() && fileExists(pluginMmdc)) {
        if (auto svg = tryExe(pluginMmdc); !svg.empty()) return svg;
    }
    if (s.mmdcPath != "mmdc") {
        if (auto svg = tryExe("mmdc"); !svg.empty()) return svg;
    }

    cleanup();
    return {};
}

// npx auto-installs @mermaid-js/mermaid-cli on first use, which needs a
// working npm registry connection and can take much longer than
// timeoutMs*3 to resolve/download -- confirmed via a live test in this same
// environment where a bare `npx -y @mermaid-js/mermaid-cli --version`
// blocked for the full duration with zero output. Kept as a last resort
// (see renderMermaid) behind the much faster and more reliable web
// fallback, instead of before it.
std::string runMermaidNpx(const Settings &s, const std::string &inputPath, bool darkMode)
{
    std::string tempConfigPath = "/tmp/diagramview_mermaid_" + std::to_string(getpid()) + ".json";
    writeFile(tempConfigPath, R"({
  "htmlLabels": false,
  "flowchart": { "htmlLabels": false },
  "sequence": { "htmlLabels": false },
  "gantt": { "htmlLabels": false },
  "journey": { "htmlLabels": false },
  "class": { "htmlLabels": false },
  "state": { "htmlLabels": false },
  "er": { "htmlLabels": false },
  "pie": { "htmlLabels": false },
  "c4": { "htmlLabels": false }
})");

    std::string outputPath = "/tmp/diagramview_mermaid_" + std::to_string(getpid()) + ".svg";

    std::vector<std::string> args = {"-y", "@mermaid-js/mermaid-cli",
                                      "-i", inputPath, "-o", outputPath, "-e", "svg"};
    if (darkMode) { args.push_back("--theme"); args.push_back("dark"); }
    if (fileExists(tempConfigPath)) { args.push_back("-c"); args.push_back(tempConfigPath); }

    ProcessResult r = runProcess("npx", args, nullptr, s.timeoutMs * 3);
    std::string svg;
    if (r.started && r.exitCode == 0 && fileExists(outputPath))
        svg = readFile(outputPath);

    remove(tempConfigPath.c_str());
    remove(outputPath.c_str());
    return svg;
}

} // namespace

std::string renderMermaid(const Settings &s, const std::string &inputPath, bool darkMode)
{
    if (s.mermaidRenderer == "web") {
        if (auto svg = runMermaidWeb(s, inputPath, darkMode); !svg.empty()) return svg;
    }
    if (auto svg = runMermaidLocal(s, inputPath, darkMode); !svg.empty()) return svg;
    if (s.mermaidRenderer != "web") {
        if (auto svg = runMermaidWeb(s, inputPath, darkMode); !svg.empty()) return svg;
    }
    // Last resort: auto-install via npx. Tried after the web fallback since
    // it is far slower and depends on npm registry access, not just a
    // single SVG-rendering HTTP request (see runMermaidNpx's comment).
    return runMermaidNpx(s, inputPath, darkMode);
}

// ── PlantUML ─────────────────────────────────────────────────────────

namespace {

std::string runPlantUmlWeb(const Settings &s, const std::string &inputPath, bool darkMode)
{
    std::string code = readFile(inputPath);
    if (code.empty()) return {};

    // The public PlantUML server has no query param for theme -- dark
    // rendering is a distinct path prefix ("dsvg" vs "svg"), confirmed by
    // diffing actual server output: /svg/ returns background:#FFFFFF with
    // stroke:#181818, /dsvg/ returns background:#1B1B1B with
    // stroke:#E7E7E7. Passing darkMode=true to --dark-mode on the local CLI
    // path but never selecting this endpoint on the web fallback path meant
    // the web fallback (the only path that actually works when no local
    // java/plantuml install is present) always rendered black-on-dark-bg.
    std::string url = s.plantumlServerUrl + (darkMode ? "/dsvg/~h" : "/svg/~h") + hexEncode(code);

    ProcessResult r = runProcess("curl",
        {"-s", "-f", "--max-time", std::to_string(s.timeoutMs / 1000), url},
        nullptr, s.timeoutMs);
    if (r.started && r.exitCode == 0)
        return r.stdoutData;
    return {};
}

std::string runPlantUmlLocal(const Settings &s, const std::string &inputPath, bool darkMode)
{
    std::string code = readFile(inputPath);
    if (code.empty()) return {};

    std::vector<std::string> pumlArgs = {"-tsvg", "-pipe"};
    if (darkMode) pumlArgs.push_back("--dark-mode");

    std::vector<std::string> exeOptions;
    if (toLower(s.plantumlPath).size() >= 4 &&
        toLower(s.plantumlPath).compare(toLower(s.plantumlPath).size() - 4, 4, ".jar") != 0) {
        exeOptions.push_back(s.plantumlPath);
    }
    exeOptions.push_back("plantuml");
    exeOptions.push_back("/usr/bin/plantuml");

    for (const auto &exe : exeOptions) {
        ProcessResult r = runProcess(exe, pumlArgs, &code, s.timeoutMs);
        if (r.started && r.exitCode == 0 && !r.stdoutData.empty())
            return r.stdoutData;
    }

    std::vector<std::string> jarOptions;
    if (toLower(s.plantumlPath).size() >= 4 &&
        toLower(s.plantumlPath).compare(toLower(s.plantumlPath).size() - 4, 4, ".jar") == 0) {
        jarOptions.push_back(s.plantumlPath);
    }
    jarOptions.push_back("/usr/share/java/plantuml/plantuml.jar");
    jarOptions.push_back("/usr/share/plantuml/plantuml.jar");
    if (!pluginDir().empty()) jarOptions.push_back(pluginDir() + "/plantuml.jar");

    for (const auto &jar : jarOptions) {
        if (!fileExists(jar)) continue;
        std::vector<std::string> jarArgs = {"-jar", jar, "-tsvg", "-pipe"};
        if (darkMode) jarArgs.push_back("--dark-mode");

        ProcessResult r = runProcess(s.javaPath, jarArgs, &code, s.timeoutMs);
        if (r.started && r.exitCode == 0 && !r.stdoutData.empty())
            return r.stdoutData;

        if (s.javaPath != "java") {
            ProcessResult r2 = runProcess("java", jarArgs, &code, s.timeoutMs);
            if (r2.started && r2.exitCode == 0 && !r2.stdoutData.empty())
                return r2.stdoutData;
        }
    }
    return {};
}

} // namespace

std::string renderPlantUml(const Settings &s, const std::string &inputPath, bool darkMode)
{
    if (s.renderer == "web") {
        if (auto svg = runPlantUmlWeb(s, inputPath, darkMode); !svg.empty()) return svg;
        return runPlantUmlLocal(s, inputPath, darkMode);
    }
    if (auto svg = runPlantUmlLocal(s, inputPath, darkMode); !svg.empty()) return svg;
    return runPlantUmlWeb(s, inputPath, darkMode);
}

// ── SVG text baseline fixup ──────────────────────────────────────────

// ── Hand-written replacements for what used to be static const std::regex
// objects in this function (textTspanRe, yRe, dyRe, textYRe, emRe). Confirmed
// via a live gdb session against the real deployed plugin (markdownview's
// identical copy of this logic, in diagram_render.cpp): constructing ANY of
// these std::regex objects crashes with SIGSEGV, on DC's own main thread,
// with a full symbolized backtrace showing regex_traits<char>::transform ->
// collate<char>::transform dispatching into
// codecvt<char16_t,char,__mbstate_t>::do_unshift -- a char-collate call
// landing in a completely unrelated char16_t codecvt facet. That is libstdc++
// locale facet-ID corruption/mismatch, not a stack or threading problem (both
// were tried first, in that order, on this exact code, and neither changed
// the crash). Every pattern here is simple enough to hand-write; doing so
// removes the dependency on libstdc++'s <regex>/<locale> machinery entirely.

// Regex `\bNAME\s*=\s*"[^"]*"`: finds the next `name="value"` (tolerant of
// whitespace around `=`) starting at `from`, requiring a non-word character
// (or start of string) immediately before `name` -- \b's hand-rolled
// equivalent.
bool findQuotedAttr(const std::string &s, const std::string &name, size_t from,
                    size_t &matchStart, size_t &matchEnd, std::string &value)
{
    size_t pos = from;
    while (true) {
        pos = s.find(name, pos);
        if (pos == std::string::npos) return false;
        if (pos > 0) {
            char prev = s[pos - 1];
            if (isalnum((unsigned char)prev) || prev == '_') { ++pos; continue; }
        }
        size_t p = pos + name.size();
        while (p < s.size() && isspace((unsigned char)s[p])) ++p;
        if (p >= s.size() || s[p] != '=') { ++pos; continue; }
        ++p;
        while (p < s.size() && isspace((unsigned char)s[p])) ++p;
        if (p >= s.size() || s[p] != '"') { ++pos; continue; }
        size_t valStart = p + 1;
        size_t valEnd = s.find('"', valStart);
        if (valEnd == std::string::npos) return false;
        matchStart = pos;
        matchEnd = valEnd + 1;
        value = s.substr(valStart, valEnd - valStart);
        return true;
    }
}

// Validates `-?[0-9]*\.?[0-9]+` starting at `start` (at least one digit
// somewhere, at most one '.', optional leading '-'). Sets numEnd to just
// past the numeric text on success.
bool isValidEmNumber(const std::string &s, size_t start, size_t &numEnd)
{
    size_t i = start;
    if (i < s.size() && s[i] == '-') ++i;
    size_t digitCount = 0;
    bool sawDot = false;
    while (i < s.size()) {
        if (isdigit((unsigned char)s[i])) { ++i; ++digitCount; }
        else if (s[i] == '.' && !sawDot) { sawDot = true; ++i; }
        else break;
    }
    if (digitCount == 0) return false;
    numEnd = i;
    return true;
}

// Regex `\bNAME\s*=\s*"(-?[0-9]*\.?[0-9]+)em"`.
bool findNumericEmAttr(const std::string &s, const std::string &name, size_t from,
                       size_t &matchStart, size_t &matchEnd, double &emValue)
{
    size_t pos = from;
    while (true) {
        pos = s.find(name, pos);
        if (pos == std::string::npos) return false;
        if (pos > 0) {
            char prev = s[pos - 1];
            if (isalnum((unsigned char)prev) || prev == '_') { ++pos; continue; }
        }
        size_t p = pos + name.size();
        while (p < s.size() && isspace((unsigned char)s[p])) ++p;
        if (p >= s.size() || s[p] != '=') { ++pos; continue; }
        ++p;
        while (p < s.size() && isspace((unsigned char)s[p])) ++p;
        if (p >= s.size() || s[p] != '"') { ++pos; continue; }
        size_t valStart = p + 1;
        size_t numEnd;
        if (!isValidEmNumber(s, valStart, numEnd) || s.compare(numEnd, 2, "em") != 0
            || numEnd + 2 >= s.size() || s[numEnd + 2] != '"') { ++pos; continue; }
        matchStart = pos;
        matchEnd = numEnd + 3; // "em" + closing quote
        emValue = safeStod(s.substr(valStart, numEnd - valStart));
        return true;
    }
}

// Regex `\b(y|dy)\s*=\s*"(-?[0-9]*\.?[0-9]+)em"`: earliest of either "y=" or
// "dy=".
bool findYOrDyEmAttr(const std::string &s, size_t from, size_t &matchStart, size_t &matchEnd,
                     std::string &attrName, double &emValue)
{
    size_t yStart, yEnd; double yVal;
    size_t dyStart, dyEnd; double dyVal;
    bool hasY = findNumericEmAttr(s, "y", from, yStart, yEnd, yVal);
    bool hasDy = findNumericEmAttr(s, "dy", from, dyStart, dyEnd, dyVal);
    if (!hasY && !hasDy) return false;
    if (hasY && (!hasDy || yStart <= dyStart)) {
        matchStart = yStart; matchEnd = yEnd; attrName = "y"; emValue = yVal;
    } else {
        matchStart = dyStart; matchEnd = dyEnd; attrName = "dy"; emValue = dyVal;
    }
    return true;
}

// Regex `<text\b([^>]*)>\s*<tspan\b([^>]*)>`.
bool findTextTspanPair(const std::string &s, size_t from, size_t &matchStart, size_t &matchEnd,
                       std::string &textAttrs, std::string &tspanAttrs)
{
    size_t pos = from;
    while (true) {
        pos = s.find("<text", pos);
        if (pos == std::string::npos) return false;
        size_t afterTag = pos + 5;
        if (afterTag < s.size() && (isalnum((unsigned char)s[afterTag]) || s[afterTag] == '_')) { pos += 5; continue; }
        size_t textAttrsEnd = s.find('>', afterTag);
        if (textAttrsEnd == std::string::npos) return false;
        std::string tAttrs = s.substr(afterTag, textAttrsEnd - afterTag);
        size_t p = textAttrsEnd + 1;
        while (p < s.size() && isspace((unsigned char)s[p])) ++p;
        if (s.compare(p, 6, "<tspan") != 0) { pos += 5; continue; }
        size_t tspanAfter = p + 6;
        if (tspanAfter < s.size() && (isalnum((unsigned char)s[tspanAfter]) || s[tspanAfter] == '_')) { pos += 5; continue; }
        size_t tspanAttrsEnd = s.find('>', tspanAfter);
        if (tspanAttrsEnd == std::string::npos) return false;
        textAttrs = tAttrs;
        tspanAttrs = s.substr(tspanAfter, tspanAttrsEnd - tspanAfter);
        matchStart = pos;
        matchEnd = tspanAttrsEnd + 1;
        return true;
    }
}

// Regex `\s+` collapse-to-single-space (mirrors QString::simplified()'s
// intent, matching the original comment here).
std::string collapseWhitespace(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    bool inSpace = false;
    for (char c : s) {
        if (isspace((unsigned char)c)) {
            if (!inSpace) out += ' ';
            inSpace = true;
        } else {
            out += c;
            inSpace = false;
        }
    }
    return out;
}

std::string removeNumericEmAttr(const std::string &s, const std::string &name)
{
    size_t ms, me; double v;
    if (!findNumericEmAttr(s, name, 0, ms, me, v)) return s;
    return s.substr(0, ms) + s.substr(me);
}

// Regex `<foreignObject\s+width="([^"]+)"\s+height="([^"]+)"[^>]*>.*?
// <span[^>]*>(?:<p>)?(.*?)(?:</p>)?</span>.*?</foreignObject>`. Ported from
// markdownview's identical helper (diagram_render.cpp) -- confirmed via a
// live test that mermaid's web renderer (mermaid.ink, the fallback actually
// used whenever no local mmdc binary is installed) always emits HTML labels
// as <foreignObject><div><span class="nodeLabel">...</span></div>, never
// plain <text>/<tspan>, regardless of theme. librsvg has no support for
// foreignObject/HTML content at all, so those labels rendered as
// completely invisible text until converted to plain <text> here.
bool findForeignObject(const std::string &s, size_t from, size_t &matchStart, size_t &matchEnd,
                       std::string &width, std::string &height, std::string &innerText)
{
    size_t pos = from;
    while (true) {
        pos = s.find("<foreignObject", pos);
        if (pos == std::string::npos) return false;
        size_t p = pos + 14;
        if (p >= s.size() || !isspace((unsigned char)s[p])) { pos += 14; continue; }
        while (p < s.size() && isspace((unsigned char)s[p])) ++p;
        if (s.compare(p, 7, "width=\"") != 0) { pos += 14; continue; }
        p += 7;
        size_t wEnd = s.find('"', p);
        if (wEnd == std::string::npos) return false;
        if (wEnd == p) { pos += 14; continue; } // width="" doesn't match [^"]+ (one or more)
        std::string w = s.substr(p, wEnd - p);
        p = wEnd + 1;
        if (p >= s.size() || !isspace((unsigned char)s[p])) { pos += 14; continue; }
        while (p < s.size() && isspace((unsigned char)s[p])) ++p;
        if (s.compare(p, 8, "height=\"") != 0) { pos += 14; continue; }
        p += 8;
        size_t hEnd = s.find('"', p);
        if (hEnd == std::string::npos) return false;
        if (hEnd == p) { pos += 14; continue; }
        std::string h = s.substr(p, hEnd - p);
        p = hEnd + 1;
        size_t tagEnd = s.find('>', p);
        if (tagEnd == std::string::npos) return false;
        p = tagEnd + 1;
        size_t spanPos = s.find("<span", p);
        if (spanPos == std::string::npos) return false;
        size_t spanTagEnd = s.find('>', spanPos);
        if (spanTagEnd == std::string::npos) return false;
        size_t contentStart = spanTagEnd + 1;
        if (s.compare(contentStart, 3, "<p>") == 0) contentStart += 3;
        size_t spanClose = s.find("</span>", contentStart);
        if (spanClose == std::string::npos) return false;
        size_t contentEnd = spanClose;
        static const std::string pClose = "</p>";
        if (contentEnd >= contentStart + pClose.size() &&
            s.compare(contentEnd - pClose.size(), pClose.size(), pClose) == 0)
            contentEnd -= pClose.size();
        size_t foClose = s.find("</foreignObject>", spanClose);
        if (foClose == std::string::npos) return false;
        matchStart = pos;
        matchEnd = foClose + 16; // strlen("</foreignObject>")
        width = w;
        height = h;
        innerText = s.substr(contentStart, contentEnd - contentStart);
        return true;
    }
}

std::string fixMermaidSvgText(const std::string &svgData, bool darkMode)
{
    std::string svg = svgData;

    // <foreignObject> HTML-label workaround -> plain <text> librsvg can render.
    {
        std::string textColor = darkMode ? "#c9d1d9" : "#333333";
        std::string out;
        size_t lastEnd = 0, searchFrom = 0;
        size_t ms, me; std::string wStr, hStr, inner;
        while (findForeignObject(svg, searchFrom, ms, me, wStr, hStr, inner)) {
            out.append(svg, lastEnd, ms - lastEnd);
            double w = safeStod(wStr);
            double h = safeStod(hStr);
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "<text x=\"%g\" y=\"%g\" dominant-baseline=\"middle\" text-anchor=\"middle\" "
                          "font-family=\"sans-serif\" font-size=\"14px\" fill=\"%s\">",
                          w / 2.0, h / 2.0 + 2.0, textColor.c_str());
            out += buf;
            out += inner;
            out += "</text>";
            lastEnd = me;
            searchFrom = me;
        }
        out.append(svg, lastEnd, std::string::npos);
        svg = out;
    }

    // Fold <text ...><tspan y="..em" dy="..em"> into an absolute pixel y=""
    // on the parent <text>, dropping y/dy from that first tspan.
    {
        std::string out;
        size_t lastEnd = 0, searchFrom = 0;
        size_t ms, me; std::string textAttrs, tspanAttrs;
        while (findTextTspanPair(svg, searchFrom, ms, me, textAttrs, tspanAttrs)) {
            size_t yMs, yMe, dyMs, dyMe; double yEm, dyEm;
            bool hasY = findNumericEmAttr(tspanAttrs, "y", 0, yMs, yMe, yEm);
            bool hasDy = findNumericEmAttr(tspanAttrs, "dy", 0, dyMs, dyMe, dyEm);
            if (hasY && hasDy) {
                double baselinePx = (yEm + dyEm) * 16.0 - 2.0;
                char buf[64];
                std::snprintf(buf, sizeof(buf), "y=\"%.2f\"", baselinePx);

                size_t textYMs, textYMe; std::string dummy;
                if (findQuotedAttr(textAttrs, "y", 0, textYMs, textYMe, dummy)) {
                    textAttrs = textAttrs.substr(0, textYMs) + buf + textAttrs.substr(textYMe);
                } else {
                    textAttrs = std::string(" ") + buf + textAttrs;
                }

                tspanAttrs = removeNumericEmAttr(tspanAttrs, "y");
                tspanAttrs = removeNumericEmAttr(tspanAttrs, "dy");
                tspanAttrs = collapseWhitespace(tspanAttrs);
                tspanAttrs = trim(tspanAttrs);
                if (!tspanAttrs.empty()) tspanAttrs = " " + tspanAttrs;

                out.append(svg, lastEnd, ms - lastEnd);
                out += "<text" + textAttrs + "><tspan" + tspanAttrs + ">";
                lastEnd = me;
            }
            searchFrom = me;
        }
        out.append(svg, lastEnd, std::string::npos);
        svg = out;
    }

    // Convert any remaining y/dy em values (outside the pattern above) to
    // pixel values in place.
    {
        std::string out;
        size_t lastEnd = 0, searchFrom = 0;
        size_t ms, me; std::string attrName; double emVal;
        while (findYOrDyEmAttr(svg, searchFrom, ms, me, attrName, emVal)) {
            out.append(svg, lastEnd, ms - lastEnd);
            double pxValue = emVal * 16.0;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s=\"%.2f\"", attrName.c_str(), pxValue);
            out += buf;
            lastEnd = me;
            searchFrom = me;
        }
        out.append(svg, lastEnd, std::string::npos);
        svg = out;
    }

    return svg;
}

} // namespace DiagramRenderer
