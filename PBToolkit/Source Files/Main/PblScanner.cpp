#include "Utils.h"
#include "Config.h"
#include "Logger.h"
#include "PblScanner.h"

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

// Default path for the local mirror directory
static const fs::path MIRROR_ROOT = Config::MIRROR_ROOT;

/**
 * Copies a file from src to dst if it does not exist or is outdated.
 * @param src Source file path
 * @param dst Destination file path
 */
void PblScanner::syncOneFile(const fs::path& src, const fs::path& dst) const
{
    try
    {
        bool mustCopy = false;

        if (!fs::exists(dst))
            mustCopy = true;
        else
        {
            auto tSrc = fs::last_write_time(src);
            auto tDst = fs::last_write_time(dst);
            if (tSrc > tDst)
                mustCopy = true;
        }

        if (mustCopy)
        {
            fs::create_directories(dst.parent_path());
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
        }
    }
    catch (...) {}
}

/**
 * Recursively syncs all files from a version folder on the remote server.
 * @param source Remote folder path to mirror
 * @param target Local mirror destination
 * @return true if completed successfully
 */
bool PblScanner::syncMirror(const fs::path& source, const fs::path& target) const
{
    if (!fs::exists(source))
    {
        Logger::Error(L"[Mirror] Remote source not found: " + source.wstring());
        return false;
    }

    // 1. Collect all files
    std::vector<fs::path> files;
    for (auto& it : fs::recursive_directory_iterator(source))
        if (it.is_regular_file())
            files.push_back(it.path());

    std::size_t total = files.size();
    if (total == 0)
        return true;

    // 2. Calculate total size in MB
    std::uintmax_t totalBytes = 0;
    for (auto& f : files)
        totalBytes += fs::file_size(f);

    Logger::Info(
        L"[Mirror] Sync " + source.filename().wstring() +
        L": " + std::to_wstring(total) + L" files (" +
        std::to_wstring(totalBytes / (1024 * 1024)) + L" MB)"
    );

    // 3. Copy files with live progress bar
    auto last = std::chrono::steady_clock::now();

    std::size_t i = 0;
    for (auto& src : files)
    {
        ++i;

        fs::path rel = src.lexically_relative(source);
        fs::path dst = target / rel;

        syncOneFile(src, dst);

        // Update progress every 200ms
        auto now = std::chrono::steady_clock::now();
        if (now - last > std::chrono::milliseconds(200))
        {
            Utils::progressBar(
                i,
                total,
                L"[" + source.filename().wstring() + L"] "
            );
            last = now;
        }
    }

    // Final progress update
    Utils::progressBar(
        total,
        total,
        L"[" + source.filename().wstring() + L"] "
    );

    std::wcout << L""; // version separator spacing

    return true;
}

/**
 * Main scanner routine. Syncs local mirror and enumerates all .PBL files inside it.
 * @return Vector of TargetInfo entries for each detected PBL
 */
std::vector<TargetInfo> PblScanner::scan() const
{
    std::vector<TargetInfo> result;

    const std::vector<std::wstring> versions = {
        Config::V65, Config::V7, Config::V8,
        Config::V9,  Config::V105, Config::V125
    };

    fs::path remoteRoot = Config::PB_ROOT;

    Logger::Info(L"Scanning remote PB root: " + remoteRoot.wstring());
    Logger::Info(L"Using local mirror: " + MIRROR_ROOT.wstring());

    if (!fs::exists(MIRROR_ROOT))
        fs::create_directories(MIRROR_ROOT);

    // 1. Sync local mirror from remote
    for (const auto& v : versions)
    {
        fs::path src = remoteRoot / v;
        fs::path dst = MIRROR_ROOT / v;

        if (!fs::exists(src))
            continue;

        Logger::Info(L"→ Sync version " + v, true);

        syncMirror(src, dst);
    }

    std::wcout << L"\n";

    // 2. Scan all local mirrored folders for .pbl files
    Logger::Info(L"Scanning local mirror...");

    for (const auto& v : versions)
    {
        fs::path versionRoot = MIRROR_ROOT / v;
        if (!fs::exists(versionRoot))
            continue;

        Logger::Info(L"  Local version " + v);

        for (const auto& it : fs::recursive_directory_iterator(versionRoot))
        {
            if (!it.is_regular_file())
                continue;

            const fs::path& p = it.path();

            if (Utils::toLower(p.extension().wstring()) == L".pbl")
            {
                TargetInfo info;
                info.version = v;
                info.name = p.stem().wstring();
                info.fullPath = p;
                result.push_back(info);
            }
        }
    }

    return result;
}