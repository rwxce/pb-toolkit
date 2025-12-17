#pragma once
#include <string>
#include <filesystem>

/**
 * @brief Global configuration class holding all key filesystem paths and version constants.
 */
class Config
{
public:
    // Root directories
    static const std::filesystem::path PB_ROOT; /// Root path for original PowerBuilder libraries
    static const std::filesystem::path SOLUTION_ROOT; /// Root path to PBToolkit.sln
    static const std::filesystem::path EXTRACT_ROOT; /// Root directory for extracted output files
    static const std::filesystem::path MIRROR_ROOT; /// Local mirror root directory

    // Resource and source folders
    static const std::filesystem::path RESOURCES_DIR; /// Base Resource Files directory
    static const std::filesystem::path SOURCE_DIR; /// Base Source Files directory (Python, utilities, etc.)

    // Subdirectories for specific export components
    static const std::filesystem::path SOURCES_DIR; /// Directory containing extracted raw source code
    static const std::filesystem::path CONVERTED_DIR; /// Directory for converted PowerBuilder sources
    static const std::filesystem::path SELECTS_DIR; /// Directory containing exported SQL SELECT statements
    static const std::filesystem::path PROJECTS_DIR; /// Directory containing generated project dependency files (PBT → PBL mappings)

    // Python environment
    static const std::filesystem::path PY_SCRIPTS_DIR; /// Directory containing Python scripts
    static const std::filesystem::path VENV_DIR; /// Python virtual environment directory
    static const std::filesystem::path PYTHON_EXE; /// Full path to python.exe

    // External tools
    static const std::filesystem::path PBLDUMP_EXE; /// Full path to PblDump.exe binary

    // Supported PowerBuilder versions
    static const inline std::wstring V65 = L"6.5";
    static const inline std::wstring V7 = L"7.0";
    static const inline std::wstring V8 = L"8.0";
    static const inline std::wstring V9 = L"9.0";
    static const inline std::wstring V105 = L"10.5";
    static const inline std::wstring V125 = L"12.5";
};