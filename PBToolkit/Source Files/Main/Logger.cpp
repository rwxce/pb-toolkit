#include "Logger.h"

#include <string>
#include <ctime>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

// Defines the log levels supported
enum class LogType { Info, Warn, Error, Debug };

/**
 * Returns the current local timestamp in format DD-MM-YYYY HH:MM:SS.
 */
std::wstring Logger::Timestamp()
{
    using namespace std::chrono;

    auto now = system_clock::now();
    auto t = system_clock::to_time_t(now);

    tm lt{};
    localtime_s(&lt, &t);

    std::wstringstream ss;
    ss << std::put_time(&lt, L"%d-%m-%Y %H:%M:%S");
    return ss.str();
}

/**
 * Internal utility to write a log line to stdout with given type and formatting.
 * @param type Log severity (Info, Warn, Error, Debug)
 * @param msg Message to log
 * @param leadingBreaks Blank lines before the log
 * @param trailingBreaks Blank lines after the log
 */
static void _LogLine(LogType type, const std::wstring& msg,
    int leadingBreaks, int trailingBreaks)
{
    for (int i = 0; i < leadingBreaks; i++)
        std::wcout << L"\n";

    switch (type)
    {
    case LogType::Info:
        std::wcout << L"[INFO][" << Logger::Timestamp() << L"] " << msg << L"\n";
        break;
    case LogType::Warn:
        std::wcout << L"[WARN][" << Logger::Timestamp() << L"] " << msg << L"\n";
        break;
    case LogType::Error:
        std::wcout << L"[ERROR][" << Logger::Timestamp() << L"] " << msg << L"\n";
        break;
    case LogType::Debug:
        std::wcout << L"[DEBUG][" << Logger::Timestamp() << L"] " << msg << L"\n";
        break;
    }

    for (int i = 0; i < trailingBreaks; i++)
        std::wcout << L"\n";

    std::wcout.flush();
}

// Logs an informational message
void Logger::Info(const std::wstring& msg, int lead, int trail)
{
    _LogLine(LogType::Info, msg, lead, trail);
}

// Logs a warning message
void Logger::Warn(const std::wstring& msg, int lead, int trail)
{
    _LogLine(LogType::Warn, msg, lead, trail);
}

// Logs an error message
void Logger::Error(const std::wstring& msg, int lead, int trail)
{
    _LogLine(LogType::Error, msg, lead, trail);
}

// Logs a debug message
void Logger::Debug(const std::wstring& msg, int lead, int trail)
{
    _LogLine(LogType::Debug, msg, lead, trail);
}