#pragma once

/// @file CrashLogger.h
/// Global exception logging for WLX plugins.
///
/// Usage in wlx_entry.cpp:
/// @code
///   #include <wlxbase_wlqt/CrashLogger.h>
///
///   HWND DCPCALL ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags)
///   {
///       WLX_TRY {
///           // ... normal plugin code ...
///       } WLX_CATCH("ListLoad");
///       return nullptr;  // fallback return on exception
///   }
/// @endcode
///
/// Logs are written to <plugin_name>.log next to the .wlx file.

#include <exception>
#include <string>

namespace QtWlPlugin {

class CrashLogger {
public:
    /// Initialize with any address inside the plugin .so/.wlx
    /// (typically a function pointer like &ListLoad).
    /// Determines the plugin path and creates the log file path.
    static void init(void *addressInPlugin);

    /// Log a caught std::exception with stack trace.
    static void log(const char *entryPoint, const std::exception &e);

    /// Log an unknown exception (catch ...) with stack trace.
    static void logUnknown(const char *entryPoint);

    /// Log a custom message (for warnings, state dumps, etc.)
    static void logMessage(const char *entryPoint, const char *message);

    /// Get the log file path (empty if not initialized).
    static const std::string &logPath();

private:
    static void writeEntry(const char *entryPoint, const char *type,
                           const char *message);
};

} // namespace QtWlPlugin

/// Convenience macros for wrapping WLX entry points.
///
/// WLX_TRY { ... } WLX_CATCH("FunctionName");
///
/// On first use, auto-initializes the logger with the current function address.
#define WLX_TRY                                                     \
    {                                                               \
        static bool _wlx_logger_init = false;                       \
        if (!_wlx_logger_init) {                                    \
            _wlx_logger_init = true;                                \
            QtWlPlugin::CrashLogger::init(                          \
                reinterpret_cast<void*>(&_wlx_logger_init));        \
        }                                                           \
    }                                                               \
    try

#define WLX_CATCH(funcName)                                         \
    catch (const std::exception &_wlx_ex) {                         \
        QtWlPlugin::CrashLogger::log(funcName, _wlx_ex);           \
    }                                                               \
    catch (...) {                                                    \
        QtWlPlugin::CrashLogger::logUnknown(funcName);              \
    }
