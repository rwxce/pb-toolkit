#pragma once
#include <string>

/**
 * Logger utility for standardized console output with timestamps.
 * Supports Info, Warning, Error and Debug levels.
 */
class Logger
{
public:
    /**
     * Logs an informational message.
     * @param msg The message to print.
     * @param leadingBreaks Number of blank lines before the message.
     * @param trailingBreaks Number of blank lines after the message.
     */
    static void Info(const std::wstring& msg, int leadingBreaks = 0, int trailingBreaks = 0);

    /**
     * Logs a warning message.
     * @param msg The message to print.
     * @param leadingBreaks Number of blank lines before the message.
     * @param trailingBreaks Number of blank lines after the message.
     */
    static void Warn(const std::wstring& msg, int leadingBreaks = 0, int trailingBreaks = 0);

    /**
     * Logs an error message.
     * @param msg The message to print.
     * @param leadingBreaks Number of blank lines before the message.
     * @param trailingBreaks Number of blank lines after the message.
     */
    static void Error(const std::wstring& msg, int leadingBreaks = 0, int trailingBreaks = 0);

    /**
     * Logs a debug message.
     * @param msg The message to print.
     * @param leadingBreaks Number of blank lines before the message.
     * @param trailingBreaks Number of blank lines after the message.
     */
    static void Debug(const std::wstring& msg, int leadingBreaks = 0, int trailingBreaks = 0);

    /**
     * Generates a timestamp string in format DD-MM-YYYY HH:MM:SS.
     * @return A wide string with the current timestamp.
     */
    static std::wstring Timestamp();
};