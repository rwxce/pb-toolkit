#include "Config.h"

#include <Windows.h>
#include <filesystem>

namespace fs = std::filesystem;

/**
 * Locates the directory containing PBToolkit.sln by walking upward from the executable path.
 * @return Absolute solution root directory
 */
static fs::path resolveSolutionRoot()
{
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);

    fs::path exePath = fs::path(buffer).lexically_normal();
    fs::path dir = exePath.parent_path();

    while (!dir.empty() && !fs::exists(dir / "PBToolkit.sln"))
        dir = dir.parent_path();

    return dir.empty() ? exePath.parent_path() : dir;
}

// Root locations
const fs::path Config::PB_ROOT = fs::path{ LR"(TODO)" }; // Root directory containing original PowerBuilder libraries (user-defined)
const fs::path Config::MIRROR_ROOT = fs::path{ LR"(C:\Users\Public\Documents\PBToolkit\mirror)" };
const fs::path Config::SOLUTION_ROOT = resolveSolutionRoot();

// Base folders (Resources / Source)
const fs::path Config::RESOURCES_DIR = Config::SOLUTION_ROOT / "PBToolkit" / "Resource Files";
const fs::path Config::SOURCE_DIR = Config::SOLUTION_ROOT / "PBToolkit" / "Source Files";

// Extraction structure
const fs::path Config::EXTRACT_ROOT = Config::RESOURCES_DIR / "Extraction";
const fs::path Config::SOURCES_DIR = Config::EXTRACT_ROOT / "Sources";
const fs::path Config::CONVERTED_DIR = Config::EXTRACT_ROOT / "Converted";
const fs::path Config::SELECTS_DIR = Config::EXTRACT_ROOT / "Selects";
const fs::path Config::PROJECTS_DIR = Config::EXTRACT_ROOT / "Projects";
const fs::path Config::AICODEBASE_DIR = Config::EXTRACT_ROOT / "AICodebase";

// Python environment
const fs::path Config::PY_SCRIPTS_DIR = Config::SOURCE_DIR / "Python";
const fs::path Config::VENV_DIR = Config::SOLUTION_ROOT / "PBToolkit" / "venv";
const fs::path Config::PYTHON_EXE = Config::VENV_DIR / "Scripts" / "python.exe";

// External tools
const fs::path Config::PBLDUMP_EXE =
Config::RESOURCES_DIR / "Libraries" / "pbldump-1.3.1stable" / "PblDump.exe";