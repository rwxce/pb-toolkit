#include "Utils.h"
#include "Config.h"
#include "Logger.h"
#include "PythonRunner.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <ios>
#include <string>
#include <limits>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <Windows.h>
#include <process.h>
#include <functional>
#include <filesystem>

namespace fs = std::filesystem;

/**
 * Ensures a directory exists; creates it when missing.
 */
static void ensureDirectory(const fs::path& dir)
{
    if (!fs::exists(dir))
    {
        Logger::Info(L"Creating directory: " + dir.wstring());
        fs::create_directories(dir);
    }
}

/**
 * Executes an external command and returns its exit code.
 * @param exe Executable path
 * @param args Argument list
 * @param searchPath Whether the system PATH should be searched
 * @return Process exit code
 */
static intptr_t execCmd(const fs::path& exe,
    const std::vector<std::wstring>& args,
    bool searchPath = false)
{
    std::vector<std::wstring> argvStorage;
    argvStorage.reserve(args.size() + 1);

    argvStorage.push_back(L"\"" + exe.wstring() + L"\"");
    for (const auto& a : args)
        argvStorage.push_back(L"\"" + a + L"\"");

    std::vector<const wchar_t*> argv;
    for (auto& s : argvStorage)
        argv.push_back(s.c_str());
    argv.push_back(nullptr);

    intptr_t rc = searchPath
        ? _wspawnvp(_P_WAIT, exe.wstring().c_str(), argv.data())
        : _wspawnv(_P_WAIT, exe.wstring().c_str(), argv.data());

    if (rc == -1)
        Logger::Error(L"[PROC] Failed to start process.");

    return rc;
}

/**
 * Displays an animated progress bar while the atomic flag remains true.
 * @param prefix Console prefix text
 * @param running Atomic flag controlling animation state
 */
static void animateProgress(const std::wstring& prefix, std::atomic<bool>& running)
{
    const std::size_t total = 40;
    std::size_t step = 0;

    while (running)
    {
        Utils::progressBarAnimated(step, total, prefix);
        Sleep(40);
        step = (step + 1) % total;
    }

    Utils::progressBarAnimated(total, total, prefix);
}

/**
 * Ensures Python venv exists and installs dependencies.
 */
static void bootstrapPython()
{
    const fs::path venv = Config::VENV_DIR;
    const fs::path pythonExe = Config::PYTHON_EXE;
    const fs::path reqFile = Config::PY_SCRIPTS_DIR / "requirements.txt";

    if (!fs::exists(pythonExe))
    {
        Logger::Warn(L"[PY] venv not found. Creating...");

        if (execCmd(L"python", { L"-m", L"venv", venv.wstring() }, true) != 0)
        {
            Logger::Error(L"[PY] Could not create venv.");
            return;
        }
    }

    if (!fs::exists(reqFile))
    {
        Logger::Warn(L"[PY] requirements.txt missing, skipping install.");
        return;
    }

    Logger::Info(L"[PY] Installing dependencies...\n");

    std::atomic<bool> running(true);
    std::thread animThread(animateProgress, L"[PY] Installing", std::ref(running));

    execCmd(
        pythonExe,
        {
            L"-m", L"pip", L"install",
            L"--quiet",
            L"--disable-pip-version-check",
            L"-r", reqFile.wstring()
        }
    );

    running = false;
    animThread.join();
    std::wcout << L"\n";
}

/**
 * Runs a Python script and returns whether it exited successfully.
 * @param scriptName Script filename inside PY_SCRIPTS_DIR
 * @param args Additional script arguments
 * @return true on success
 */
bool PythonRunner::runScript(const std::wstring& scriptName,
    const std::vector<std::wstring>& args)
{
    fs::path scriptPath = Config::PY_SCRIPTS_DIR / scriptName;

    if (!fs::exists(scriptPath))
    {
        Logger::Error(L"Python script not found: " + scriptPath.wstring());
        return false;
    }

    // Ensure directory structure exists
    ensureDirectory(Config::EXTRACT_ROOT);
    ensureDirectory(Config::CONVERTED_DIR);
    ensureDirectory(Config::SOURCES_DIR);
    ensureDirectory(Config::SELECTS_DIR);
    ensureDirectory(Config::AICODEBASE_DIR);

    Logger::Info(L"[PY] Executing script: " + scriptName);

    std::atomic<bool> running(true);
    std::thread animThread(animateProgress, L"[PY] " + scriptName, std::ref(running));

    std::vector<std::wstring> pythonArgs;
    pythonArgs.push_back(scriptPath.wstring());
    pythonArgs.insert(pythonArgs.end(), args.begin(), args.end());

    intptr_t exitCode = execCmd(Config::PYTHON_EXE, pythonArgs);

    running = false;
    animThread.join();

    std::wcout << L"\n";

    if (exitCode != 0)
    {
        Logger::Error(L"Python script failed with exit code "
            + std::to_wstring(exitCode));
        return false;
    }

    return true;
}

/**
 * Prints the Python menu banner.
 */
static void printPythonHeader()
{
    system("cls");

    std::wcout <<
        L"  ____  ____ _____           _ _    _ _   \n"
        L" |  _ \\| __ )_   _|__   ___ | | | _(_) |_ \n"
        L" | |_) |  _ \\ | |/ _ \\ / _ \\| | |/ / | __|\n"
        L" |  __/| |_) || | (_) | (_) | |   <| | |_ \n"
        L" |_|   |____/ |_|\\___/ \\___/|_|_|\\_\\_|\\__|\n"
        L"\n"
        L"============== PYTHON MENU ==============\n\n";
}

/**
 * Script definition struct.
 */
struct PyScriptDef {
    std::wstring name;
    std::vector<std::wstring> args;
};

/**
 * Returns a lowercase copy of the given text.
 */
static std::wstring toLower(const std::wstring& value)
{
    std::wstring out = value;
    std::transform(out.begin(), out.end(), out.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
        });
    return out;
}

/**
 * Applies the same sanitization strategy used by Python output naming.
 */
static std::wstring sanitizePbName(const std::wstring& input)
{
    std::wstring out;
    out.reserve(input.size());
    bool lastUnderscore = false;

    for (wchar_t c : input)
    {
        const bool forbidden = (c == L'<' || c == L'>' || c == L':' || c == L'"' ||
            c == L'/' || c == L'\\' || c == L'|' || c == L'?' || c == L'*');
        const bool space = std::iswspace(c) != 0;

        wchar_t next = c;
        if (forbidden || space)
            next = L'_';

        if (next == L'_')
        {
            if (lastUnderscore)
                continue;
            lastUnderscore = true;
        }
        else
        {
            lastUnderscore = false;
        }

        out.push_back(next);
    }

    while (!out.empty() && out.front() == L'_')
        out.erase(out.begin());
    while (!out.empty() && out.back() == L'_')
        out.pop_back();

    return out;
}

/**
 * Generic numbered menu selector with Back option.
 * @param title Menu title text
 * @param options Available choices
 * @param outIndex Selected 0-based index when true is returned
 * @return true if a valid option was selected; false when Back is chosen
 */
static bool selectNumberedOption(
    const std::wstring& title,
    const std::vector<std::wstring>& options,
    std::size_t& outIndex)
{
    if (options.empty())
        return false;

    while (true)
    {
        printPythonHeader();
        std::wcout << title << L"\n\n";

        for (std::size_t i = 0; i < options.size(); ++i)
            std::wcout << L" " << (i + 1) << L" - " << options[i] << L"\n";

        std::wcout << L" 0 - Back\n\n> Select option: ";

        int raw = -1;
        std::wcin >> raw;
        std::wcin.ignore(std::numeric_limits<std::streamsize>::max(), L'\n');

        if (raw == 0)
            return false;

        if (raw < 1 || static_cast<std::size_t>(raw) > options.size())
        {
            Logger::Warn(L"Invalid option.");
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            continue;
        }

        outIndex = static_cast<std::size_t>(raw - 1);
        return true;
    }
}

/**
 * Discovers PBW projects for a version and returns sanitized names.
 */
static std::vector<std::wstring> discoverAicodebaseProjects(const std::wstring& version)
{
    std::vector<std::wstring> projects;

    const fs::path versionDir = Config::MIRROR_ROOT / version;
    if (!fs::exists(versionDir))
        return projects;

    std::error_code ec;
    fs::recursive_directory_iterator it(versionDir, ec);
    fs::recursive_directory_iterator end;

    while (!ec && it != end)
    {
        const auto& entry = *it;
        if (entry.is_regular_file(ec))
        {
            const std::wstring ext = toLower(entry.path().extension().wstring());
            if (ext == L".pbw")
            {
                const std::wstring candidate = sanitizePbName(entry.path().stem().wstring());
                if (!candidate.empty())
                {
                    const auto found = std::find(projects.begin(), projects.end(), candidate);
                    if (found == projects.end())
                        projects.push_back(candidate);
                }
            }
        }
        it.increment(ec);
    }

    std::sort(projects.begin(), projects.end(), [](const std::wstring& a, const std::wstring& b) {
        return toLower(a) < toLower(b);
        });

    return projects;
}

/**
 * Builds extract_aicodebase.py args from submenu choices.
 * @param script Script definition containing base arguments
 * @param outArgs Final argument vector to execute
 * @param userCancelled True when user selected Back
 * @return true when arguments are fully prepared
 */
static bool tryBuildAicodebaseArgs(
    const PyScriptDef& script,
    std::vector<std::wstring>& outArgs,
    bool& userCancelled)
{
    userCancelled = false;

    if (script.args.size() < 3)
    {
        Logger::Error(L"extract_aicodebase.py requires 3 base arguments.");
        return false;
    }

    std::vector<std::wstring> baseArgs = {
        script.args[0], script.args[1], script.args[2]
    };

    while (true)
    {
        printPythonHeader();
        std::wcout << L"============ AICODEBASE MENU ============\n\n";
        std::wcout << L"1 - Regenerate all\n";
        std::wcout << L"2 - Regenerate one version\n";
        std::wcout << L"3 - Regenerate one project in a version\n";
        std::wcout << L"0 - Back\n\n";
        std::wcout << L"> Select option: ";

        int opt = -1;
        std::wcin >> opt;
        std::wcin.ignore(std::numeric_limits<std::streamsize>::max(), L'\n');

        if (opt == 0)
        {
            userCancelled = true;
            return false;
        }

        if (opt == 1)
        {
            outArgs = baseArgs;
            outArgs.push_back(L"all");
            return true;
        }

        if (opt == 2)
        {
            std::size_t idx = 0;
            if (!selectNumberedOption(L"Select version:", Config::SUPPORTED_VERSIONS, idx))
                continue;

            outArgs = baseArgs;
            outArgs.push_back(L"version");
            outArgs.push_back(Config::SUPPORTED_VERSIONS[idx]);
            return true;
        }

        if (opt == 3)
        {
            std::size_t versionIdx = 0;
            if (!selectNumberedOption(L"Select version:", Config::SUPPORTED_VERSIONS, versionIdx))
                continue;

            const std::wstring version = Config::SUPPORTED_VERSIONS[versionIdx];
            const auto projects = discoverAicodebaseProjects(version);
            if (projects.empty())
            {
                Logger::Warn(L"No PBW projects found for selected version.");
                std::this_thread::sleep_for(std::chrono::milliseconds(900));
                continue;
            }

            std::size_t projectIdx = 0;
            if (!selectNumberedOption(L"Select project:", projects, projectIdx))
                continue;

            outArgs = baseArgs;
            outArgs.push_back(L"project");
            outArgs.push_back(version);
            outArgs.push_back(projects[projectIdx]);
            return true;
        }

        Logger::Warn(L"Invalid option.");
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
    }
}

/**
 * Runtime generator of Python script list (fixes static init order issues).
 */
static std::vector<PyScriptDef> getPythonScripts()
{
    return {
        {
            L"extract_pbt_dependencies.py",
            {
                Config::MIRROR_ROOT.wstring(),
                Config::PROJECTS_DIR.wstring()
            }
        },
        {
            L"extract_aicodebase.py",
            {
                Config::MIRROR_ROOT.wstring(),
                Config::SOURCES_DIR.wstring(),
                Config::AICODEBASE_DIR.wstring()
            }
        },
        {
            L"combine_to_files.py",
            {
                Config::SOURCES_DIR.wstring(),
                Config::CONVERTED_DIR.wstring()
            }
        },
        {
            L"extract_selects.py",
            {
                Config::CONVERTED_DIR.wstring(),
                Config::SELECTS_DIR.wstring()
            }
        },
        {
            L"summarize_selects.py",
            {
                Config::SELECTS_DIR.wstring(),
                Config::PROJECTS_DIR.wstring()
            }
        },
        {
            L"extract_table_values.py",
            {
                Config::CONVERTED_DIR.wstring(),
                Config::PROJECTS_DIR.wstring()
            }
        }
    };
}

/**
 * Executes the complete Python pipeline.
 * For extract_aicodebase.py, forces explicit "all" mode.
 */
bool PythonRunner::runFullPipeline()
{
    bootstrapPython();

    bool success = true;

    for (const auto& script : getPythonScripts())
    {
        std::vector<std::wstring> argsToRun = script.args;
        if (script.name == L"extract_aicodebase.py")
        {
            if (argsToRun.size() >= 3)
            {
                argsToRun = { argsToRun[0], argsToRun[1], argsToRun[2], L"all" };
            }
            else
            {
                Logger::Error(L"extract_aicodebase.py requires 3 base arguments.");
                success = false;
                break;
            }
        }

        if (!runScript(script.name, argsToRun))
        {
            success = false;
            break;
        }
    }

    std::wcout << L"\nPipeline finished. Press Enter to continue...";
    std::wcin.get();

    return success;
}

/**
 * Prints all .py files in PY_SCRIPTS_DIR.
 */
void PythonRunner::printScripts()
{
    printPythonHeader();
    std::wcout << L"Available Python scripts:\n\n";

    for (auto& entry : fs::directory_iterator(Config::PY_SCRIPTS_DIR))
        if (entry.is_regular_file() && entry.path().extension() == L".py")
            std::wcout << L"  - " << entry.path().filename().wstring() << L"\n";

    std::wcout << L"\nPress Enter to continue...";
    std::wcin.get();
}

/**
 * Allows executing a single script from the menu.
 */
bool PythonRunner::runSingleScript()
{
    while (true)
    {
        printPythonHeader();

        auto scripts = getPythonScripts();

        std::wcout << L"Available scripts:\n\n";
        for (size_t i = 0; i < scripts.size(); ++i)
            std::wcout << L" " << (i + 1) << L" - " << scripts[i].name << L"\n";

        std::wcout << L" 0 - Back\n\n> Select script: ";

        int choice = -1;
        std::wcin >> choice;
        std::wcin.ignore(std::numeric_limits<std::streamsize>::max(), L'\n');

        if (choice == 0)
            return false;

        if (choice < 1)
        {
            Logger::Warn(L"Invalid option.");
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            continue;
        }

        const std::size_t choiceIdx = static_cast<std::size_t>(choice);

        if (choiceIdx > scripts.size())
        {
            Logger::Warn(L"Invalid option.");
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            continue;
        }

        const auto& script = scripts[choiceIdx - 1];
                std::vector<std::wstring> argsToRun = script.args;

        if (script.name == L"extract_aicodebase.py")
        {
            bool userCancelled = false;
            if (!tryBuildAicodebaseArgs(script, argsToRun, userCancelled))
            {
                if (userCancelled)
                    continue;

                std::wcout << L"\nScript could not be configured. Press Enter to continue...";
                std::wcin.get();
                return false;
            }
        }

        printPythonHeader();
        Logger::Info(L"Running script: " + script.name);

        bool ok = runScript(script.name, argsToRun);

        std::wcout << L"\nScript finished. Press Enter to continue...";
        std::wcin.get();

        return ok;
    }
}

/**
 * Deletes the existing Python virtual environment and rebuilds it fresh.
 */
bool PythonRunner::rebuildVenv()
{
    printPythonHeader();
    Logger::Warn(L"[PY] Rebuilding virtual environment...");

    // 1. Delete venv
    try
    {
        fs::remove_all(Config::VENV_DIR);
    }
    catch (...)
    {
        Logger::Error(L"[PY] Failed to delete venv directory.");
        return false;
    }

    // 2. Recreate venv + reinstall dependencies
    bootstrapPython();

    std::wcout << L"\nPython venv rebuild complete.\nPress Enter to continue...";
    std::wcin.get();
    return true;
}

/**
 * Interactive Python submenu.
 */
void PythonRunner::menu()
{
    while (true)
    {
        printPythonHeader();

        std::wcout << L"1 - Run full Python pipeline\n";
        std::wcout << L"2 - Run a single script\n";
        std::wcout << L"3 - List available scripts\n";
        std::wcout << L"4 - Rebuild Python venv\n";
        std::wcout << L"0 - Back to main menu\n\n";
        std::wcout << L"> Select option: ";

        int opt = -1;
        std::wcin >> opt;
        std::wcin.ignore(std::numeric_limits<std::streamsize>::max(), L'\n');

        switch (opt)
        {
        case 1:
            printPythonHeader();
            runFullPipeline();
            break;

        case 2:
            runSingleScript();
            break;

        case 3:
            printScripts();
            break;

        case 4:
            rebuildVenv();
            break;

        case 0:
            return;

        default:
            Logger::Warn(L"Invalid option");
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
        }
    }
}