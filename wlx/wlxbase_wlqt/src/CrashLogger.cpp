#include <wlxbase_wlqt/CrashLogger.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <mutex>

#ifndef _WIN32
#include <dlfcn.h>
#include <execinfo.h>
#include <cxxabi.h>
#include <libgen.h>
#else
#include <windows.h>
#endif

namespace QtWlPlugin {

static std::string s_logPath;
static std::string s_pluginName;
static std::mutex  s_mutex;

void CrashLogger::init(void *addressInPlugin)
{
    if (!s_logPath.empty())
        return; // already initialized

#ifndef _WIN32
    Dl_info info;
    if (dladdr(addressInPlugin, &info) && info.dli_fname) {
        // Plugin path: /path/to/build/myplugin_qt6.wlx
        std::string pluginPath(info.dli_fname);

        // Extract directory
        char *pathCopy = strdup(pluginPath.c_str());
        std::string dir(dirname(pathCopy));
        free(pathCopy);

        // Extract plugin name (without extension)
        char *baseCopy = strdup(pluginPath.c_str());
        std::string base(basename(baseCopy));
        free(baseCopy);

        auto dot = base.rfind('.');
        if (dot != std::string::npos)
            base = base.substr(0, dot);

        s_pluginName = base;
        s_logPath = dir + "/" + base + ".log";
    }
#else
    HMODULE hModule = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(addressInPlugin), &hModule);
    if (hModule) {
        char path[MAX_PATH];
        GetModuleFileNameA(hModule, path, MAX_PATH);
        std::string pluginPath(path);

        auto lastSlash = pluginPath.rfind('\\');
        std::string dir = (lastSlash != std::string::npos) ? pluginPath.substr(0, lastSlash) : ".";
        std::string base = (lastSlash != std::string::npos) ? pluginPath.substr(lastSlash + 1) : pluginPath;

        auto dot = base.rfind('.');
        if (dot != std::string::npos)
            base = base.substr(0, dot);

        s_pluginName = base;
        s_logPath = dir + "\\" + base + ".log";
    }
#endif
}

const std::string &CrashLogger::logPath()
{
    return s_logPath;
}

/// Format a backtrace into the log.
static std::string captureBacktrace()
{
    std::string result;
#ifndef _WIN32
    void *frames[64];
    int count = backtrace(frames, 64);
    char **symbols = backtrace_symbols(frames, count);

    if (symbols) {
        // Skip first 3 frames (captureBacktrace, writeEntry, log/logUnknown)
        for (int i = 3; i < count; ++i) {
            result += "    ";

            // Try to demangle the symbol
            // Format: "./libfoo.so(+0x1234) [0x7fff1234]"
            // or:     "./libfoo.so(_ZN3Foo3barEv+0x42) [0x7fff1234]"
            std::string sym(symbols[i]);
            auto lparen = sym.find('(');
            auto plus = sym.find('+', lparen != std::string::npos ? lparen : 0);
            auto rparen = sym.find(')', lparen != std::string::npos ? lparen : 0);

            if (lparen != std::string::npos && rparen != std::string::npos
                && plus != std::string::npos && plus > lparen + 1) {
                std::string mangled = sym.substr(lparen + 1, plus - lparen - 1);
                int status = -1;
                char *demangled = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
                if (status == 0 && demangled) {
                    result += sym.substr(0, lparen + 1);
                    result += demangled;
                    result += sym.substr(plus);
                    free(demangled);
                } else {
                    result += sym;
                }
            } else {
                result += sym;
            }
            result += "\n";
        }
        free(symbols);
    }
#endif
    return result;
}

void CrashLogger::writeEntry(const char *entryPoint, const char *type,
                              const char *message)
{
    if (s_logPath.empty())
        return;

    std::lock_guard<std::mutex> lock(s_mutex);

    FILE *f = fopen(s_logPath.c_str(), "a");
    if (!f)
        return;

    // Timestamp
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm);

    fprintf(f, "--- %s [%s] %s ---\n", timebuf, s_pluginName.c_str(), entryPoint);
    fprintf(f, "  Type: %s\n", type);
    if (message && message[0])
        fprintf(f, "  Message: %s\n", message);

    // Stack trace
    std::string bt = captureBacktrace();
    if (!bt.empty()) {
        fprintf(f, "  Stack trace:\n%s", bt.c_str());
    }

    fprintf(f, "\n");
    fclose(f);

    // Also print to stderr so it shows in console
    fprintf(stderr, "[%s] EXCEPTION in %s: %s: %s\n",
            s_pluginName.c_str(), entryPoint, type,
            (message && message[0]) ? message : "(no message)");
}

void CrashLogger::log(const char *entryPoint, const std::exception &e)
{
    // Try to get the demangled exception type name
    const char *typeName = "std::exception";
#ifndef _WIN32
    int status = -1;
    char *demangled = abi::__cxa_demangle(typeid(e).name(), nullptr, nullptr, &status);
    if (status == 0 && demangled) {
        writeEntry(entryPoint, demangled, e.what());
        free(demangled);
        return;
    }
#endif
    typeName = typeid(e).name();
    writeEntry(entryPoint, typeName, e.what());
}

void CrashLogger::logUnknown(const char *entryPoint)
{
    writeEntry(entryPoint, "unknown (catch ...)", "Non-std::exception object thrown");
}

void CrashLogger::logMessage(const char *entryPoint, const char *message)
{
    writeEntry(entryPoint, "info", message);
}

} // namespace QtWlPlugin
