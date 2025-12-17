#pragma once
#include <string>
#include <filesystem>

/**
 * Responsible for invoking the external PblDump tool to extract contents from a .PBL file.
 */
class ScriptGenerator {
public:
    /**
     * Constructor
     * @param version PowerBuilder version string (e.g., "6.5", "7.0")
     * @param pblPath Full path to the .PBL file to process
     */
    ScriptGenerator(const std::wstring& version, const std::filesystem::path& pblPath);

    /**
     * Builds and executes the PblDump command for extraction.
     * @return true if successful, false on failure
     */
    bool generateAndRun();

    /**
     * Indicates whether the last run encountered an error.
     */
    bool hadError() const { return m_hadError; }

private:
    std::wstring m_version; /// PB version string
    std::filesystem::path m_pblPath; /// Input .PBL file path
    std::filesystem::path m_outputDir; /// Target output folder
    bool m_hadError = false; /// Error state for last execution
};