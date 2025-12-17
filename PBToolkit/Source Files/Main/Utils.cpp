#include "Utils.h"

#include <cctype>
#include <string>
#include <sstream>
#include <cstdlib>
#include <iostream>
#include <malloc.h>
#include <Windows.h>
#include <filesystem>

namespace Utils
{
    /**
     * Converts a wide string to lowercase.
     */
    std::wstring toLower(const std::wstring& s)
    {
        std::wstring out = s;
        for (auto& ch : out)
            ch = static_cast<wchar_t>(towlower(ch));
        return out;
    }

    /**
     * Converts a narrow (multi-byte) string to wide (UTF-16).
     */
    std::wstring fromNarrow(const std::string& s)
    {
        if (s.empty()) return L"";

        int len = MultiByteToWideChar(
            CP_ACP, 0,
            s.c_str(),
            static_cast<int>(s.size()),
            nullptr, 0
        );

        std::wstring out(len, L'\0');

        MultiByteToWideChar(
            CP_ACP, 0,
            s.c_str(),
            static_cast<int>(s.size()),
            &out[0], len
        );

        return out;
    }

    /**
     * Converts a wide (UTF-16) string to narrow (multi-byte).
     */
    std::string toNarrow(const std::wstring& s)
    {
        if (s.empty()) return "";

        int len = WideCharToMultiByte(
            CP_ACP, 0,
            s.c_str(),
            static_cast<int>(s.size()),
            nullptr, 0, nullptr, nullptr
        );

        std::string out(len, '\0');

        WideCharToMultiByte(
            CP_ACP, 0,
            s.c_str(),
            static_cast<int>(s.size()),
            &out[0], len,
            nullptr, nullptr
        );

        return out;
    }

    /**
     * Ensures a directory exists. Creates it if necessary.
     */
    bool ensureDir(const std::filesystem::path& dir)
    {
        try
        {
            if (std::filesystem::exists(dir))
                return true;

            return std::filesystem::create_directories(dir);
        }
        catch (...)
        {
            std::wcerr << L"[ERROR] Cannot create directory: "
                << dir.wstring() << std::endl;
            return false;
        }
    }

    /**
     * Resolves relative path to absolute path from current working directory.
     */
    std::filesystem::path toAbsolute(const std::filesystem::path& p)
    {
        if (p.is_absolute())
            return p;

        try
        {
            auto base = std::filesystem::current_path();
            return base / p;
        }
        catch (...)
        {
            return p;
        }
    }

    /**
     * Appends the given path to the current process's PATH variable.
     */
    bool AddToPath(const std::filesystem::path& dir)
    {
        if (dir.empty())
            return false;

        const std::wstring newDir = dir.wstring();
        if (newDir.empty())
            return false;

        wchar_t* oldPath = nullptr;
        size_t   len = 0;

        if (_wdupenv_s(&oldPath, &len, L"PATH") != 0 || !oldPath)
        {
            std::wcerr << L"[ERROR] Failed to read PATH" << std::endl;
            return false;
        }

        std::wstring currentPath = oldPath;
        free(oldPath);

        // Avoid duplicate entries (case-insensitive check)
        std::wstring currentLower = toLower(currentPath);
        std::wstring dirLower = toLower(newDir);

        if (currentLower.find(dirLower) != std::wstring::npos)
        {
            return true;
        }

        std::wstring combined = newDir + L";" + currentPath;

        if (_wputenv_s(L"PATH", combined.c_str()) != 0)
        {
            std::wcerr << L"[ERROR] Failed to update PATH" << std::endl;
            return false;
        }

        return true;
    }

    /**
     * Displays a dynamic progress bar in the console.
     * @param current Current index
     * @param total Total count
     * @param prefix Optional prefix string
     * @param speedMBps Optional speed in MB/s
     */
    void progressBar(std::size_t current, std::size_t total,
        const std::wstring& prefix,
        double speedMBps)
    {
        if (total == 0) return;
        double ratio = (double)current / (double)total;
        int barWidth = 40;

        int filled = (int)(ratio * barWidth);

        std::wstringstream ss;
        ss << prefix << L" [";

        for (int i = 0; i < filled; ++i) ss << L"█";
        for (int i = filled; i < barWidth; ++i) ss << L"░";

        ss << L"] " << (int)(ratio * 100.0) << L"%";
        ss << L" (" << current << L"/" << total << L")";

        if (speedMBps > 0.0)
            ss << L"  " << (int)speedMBps << L" MB/s";

        std::wcout << L"\r" << ss.str() << std::flush;

        if (current == total)
            std::wcout << std::endl;
    }

    /**
     * Animated progress bar with percentage but without showing (current/total).
     * @param current Animation frame (0..total)
     * @param total Total animation frames
     * @param prefix Prefix text before the bar
     */
    void progressBarAnimated(std::size_t current, std::size_t total,
        const std::wstring& prefix)
    {
        if (total == 0) return;

        double ratio = (double)current / (double)total;
        int barWidth = 40;

        int filled = (int)(ratio * barWidth);

        std::wstringstream ss;
        ss << prefix << L" [";

        for (int i = 0; i < filled; ++i)
            ss << L"█";

        for (int i = filled; i < barWidth; ++i)
            ss << L"░";

        ss << L"] " << (int)(ratio * 100.0) << L"%";

        std::wcout << L"\r" << ss.str() << std::flush;

        if (current == total)
            std::wcout << std::endl;
    }
}