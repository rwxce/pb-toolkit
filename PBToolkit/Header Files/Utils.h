#pragma once
#include <string>
#include <filesystem>

/**
 * Utility functions for string conversions, path handling and console output.
 */
namespace Utils
{
    /**
     * Converts a wide string to lowercase.
     * @param s Input wide string
     * @return Lowercase version
     */
    std::wstring toLower(const std::wstring& s);

    /**
     * Converts a narrow (UTF-8) string to wide (UTF-16).
     * @param s UTF-8 encoded string
     * @return UTF-16 wide string
     */
    std::wstring fromNarrow(const std::string& s);

    /**
     * Converts a wide (UTF-16) string to narrow (UTF-8).
     * @param s UTF-16 encoded string
     * @return UTF-8 narrow string
     */
    std::string toNarrow(const std::wstring& s);

    /**
     * Ensures a directory exists. Creates it if missing.
     * @param dir Target directory
     * @return true if already exists or was created successfully
     */
    bool ensureDir(const std::filesystem::path& dir);

    /**
     * Converts a relative path to an absolute path.
     * @param p Input path
     * @return Absolute version
     */
    std::filesystem::path toAbsolute(const std::filesystem::path& p);

    /**
     * Appends a directory to the system PATH environment variable.
     * @param dir Directory to append
     * @return true if successful
     */
    bool AddToPath(const std::filesystem::path& dir);

    /**
     * Renders a simple console progress bar with optional speed indicator.
     * @param current Current iteration
     * @param total Total number of steps
     * @param prefix Optional label shown before the bar
     * @param speedMBps Optional speed in MB/s (shown at the end)
     */
    void progressBar(std::size_t current,
        std::size_t total,
        const std::wstring& prefix = L"",
        double speedMBps = -1.0);

    /**
     * Renders an animated progress bar showing ONLY % completion, WITHOUT (current/total) numbers.
     * @param current Current animation frame
     * @param total Total frames in animation cycle
     * @param prefix Optional label shown before the bar
     */
    void progressBarAnimated(std::size_t current,
        std::size_t total,
        const std::wstring& prefix = L"");
}