#include "Logger.h"
#include "Config.h"
#include "MirrorManager.h"

#include <chrono>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

/**
 * Constructor: sets up paths and supported versions.
 */
MirrorManager::MirrorManager()
{
    remoteRoot = Config::PB_ROOT;
    mirrorRoot = Config::MIRROR_ROOT;

    versions = {
        Config::V65, Config::V7, Config::V8,
        Config::V9,  Config::V105, Config::V125
    };
}

/**
 * Main entry point: sync all configured PB version directories from remote to local mirror.
 * @return True if syncing was successful, false if remote root is not accessible.
 */
bool MirrorManager::syncAll()
{
    Logger::Info(L"[Mirror] Initializing mirror: " + mirrorRoot.wstring());

    if (!fs::exists(mirrorRoot))
    {
        Logger::Info(L"[Mirror] Creating mirror root...");
        fs::create_directories(mirrorRoot);
    }

    if (!fs::exists(remoteRoot))
    {
        Logger::Error(L"[Mirror] Remote PB_ROOT does not exist: " +
            remoteRoot.wstring());
        return false;
    }

    for (const auto& v : versions)
    {
        fs::path src = remoteRoot / v;
        fs::path dst = mirrorRoot / v;

        if (!fs::exists(src))
        {
            Logger::Warn(L"[Mirror] Version folder missing on remote: " + v);
            continue;
        }

        Logger::Info(L"[Mirror] Sync version " + v + L"...");

        if (!fs::exists(dst))
            fs::create_directories(dst);

        syncFolder(src, dst);
    }

    return true;
}

/**
 * Recursively copies directory content from source to destination. Skips files/folders with access issues.
 * @param src Source directory path
 * @param dst Destination directory path
 * @return True on success, false if an exception occurs
 */
bool MirrorManager::syncFolder(const fs::path& src,
    const fs::path& dst)
{
    fs::directory_options opts = fs::directory_options::skip_permission_denied;

    try
    {
        if (fs::exists(dst) && !fs::is_directory(dst))
            fs::remove(dst);

        if (!fs::exists(dst))
            fs::create_directories(dst);

        // Remove stale entries that no longer exist in source.
        std::vector<fs::path> dstEntries;
        for (auto it = fs::recursive_directory_iterator(dst, opts);
            it != fs::recursive_directory_iterator(); ++it)
        {
            dstEntries.push_back(it->path());
        }

        std::sort(dstEntries.begin(), dstEntries.end(),
            [](const fs::path& a, const fs::path& b)
            {
                return a.native().size() > b.native().size();
            });

        for (const auto& pDst : dstEntries)
        {
            fs::path rel = pDst.lexically_relative(dst);
            fs::path pSrc = src / rel;

            bool mustDelete = false;
            if (!fs::exists(pSrc))
                mustDelete = true;
            else
            {
                bool srcIsDir = fs::is_directory(pSrc);
                bool dstIsDir = fs::is_directory(pDst);
                if (srcIsDir != dstIsDir)
                    mustDelete = true;
            }

            if (!mustDelete)
                continue;

            if (fs::is_directory(pDst))
                fs::remove_all(pDst);
            else
                fs::remove(pDst);
        }

        for (auto it = fs::recursive_directory_iterator(src, opts);
            it != fs::recursive_directory_iterator(); ++it)
        {
            fs::path pSrc = it->path();
            fs::path rel = pSrc.lexically_relative(src);
            fs::path pDst = dst / rel;

            if (it->is_directory())
            {
                fs::create_directories(pDst);
                continue;
            }

            if (it->is_regular_file())
            {
                syncFile(pSrc, pDst);
            }
        }
    }
    catch (...)
    {
        Logger::Error(L"[Mirror] syncFolder failed for: " + src.wstring());
        return false;
    }

    return true;
}

/**
 * Copies a file from source to destination, only if the source is newer or destination doesn't exist.
 * @param src Source file path
 * @param dst Destination file path
 */
void MirrorManager::syncFile(const fs::path& src,
    const fs::path& dst)
{
    try
    {
        if (!fs::exists(dst))
        {
            fs::create_directories(dst.parent_path());
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
            return;
        }

        auto tSrc = fs::last_write_time(src);
        auto tDst = fs::last_write_time(dst);

        if (tSrc > tDst)
        {
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
        }
    }
    catch (...)
    {
        Logger::Warn(L"[Mirror] Could not sync file: " + src.wstring());
    }
}