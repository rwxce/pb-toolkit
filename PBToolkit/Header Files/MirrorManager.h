#pragma once
#include <string>
#include <vector>
#include <filesystem>

/**
 * Handles synchronization of PowerBuilder versioned folders from a remote location to a local mirror.
 */
class MirrorManager
{
public:
    /**
     * Constructs a MirrorManager with predefined remote and local paths.
     */
    MirrorManager();

    /**
     * Synchronizes all supported PB version folders from the remote root to the local mirror.
     * @return True if all versions were synced successfully, false otherwise.
     */
    bool syncAll();

    /**
     * Returns the path to the local mirror root.
     * @return Filesystem path of the mirror root.
     */
    std::filesystem::path getMirrorRoot() const { return mirrorRoot; }

private:
    std::filesystem::path remoteRoot; ///< Root directory on the remote root
    std::filesystem::path mirrorRoot; ///< Local destination for mirroring
    std::vector<std::wstring> versions; ///< List of supported PB versions

private:
    /**
     * Synchronizes the contents of a single folder.
     * @param src Source directory path
     * @param dst Destination directory path
     * @return True if the folder was synced successfully
     */
    bool syncFolder(const std::filesystem::path& src,
        const std::filesystem::path& dst);

    /**
     * Copies a file from source to destination, overwriting only if needed.
     * @param src Source file path
     * @param dst Destination file path
     */
    void syncFile(const std::filesystem::path& src,
        const std::filesystem::path& dst);
};