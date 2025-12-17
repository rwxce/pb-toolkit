#pragma once
#include <string>
#include <vector>
#include <filesystem>

/**
 * Struct that represents metadata for a PBL target.
 */
struct TargetInfo
{
    std::wstring version; /// PowerBuilder version (e.g., "6.5", "10.5", etc.)
    std::wstring name; /// PBL name (e.g., "users.pbl")
    std::filesystem::path fullPath; /// Absolute path to the PBL file
};

/**
 * Scans the local mirror for all PBL files and gathers basic info about them.
 */
class PblScanner
{
public:
    /**
     * Scans the local mirror and returns a list of PBL metadata entries.
     * @return Vector of TargetInfo with discovered PBLs.
     */
    std::vector<TargetInfo> scan() const;

private:
    /**
     * Synchronizes one folder to another. Used to support incremental copies if needed.
     * @param source Source directory to read from.
     * @param target Target directory to write to.
     * @return True if sync completed without exceptions.
     */
    bool syncMirror(
        const std::filesystem::path& source,
        const std::filesystem::path& target) const;

    /**
     * Copies a single file from source to destination if newer or missing.
     * @param src Source file path.
     * @param dst Destination file path.
     */
    void syncOneFile(
        const std::filesystem::path& src,
        const std::filesystem::path& dst) const;
};