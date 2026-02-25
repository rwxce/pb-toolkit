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
#include <cstdlib>
#include <iostream>
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
 */
bool PythonRunner::runFullPipeline()
{
    bootstrapPython();

    bool success = true;

    for (const auto& script : getPythonScripts())
    {
        if (!runScript(script.name, script.args))
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

        printPythonHeader();
        Logger::Info(L"Running script: " + script.name);

        bool ok = runScript(script.name, script.args);

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