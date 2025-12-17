#include "Utils.h"
#include "Config.h"
#include "Logger.h"
#include "PblScanner.h"
#include "MenuHandler.h"
#include "PythonRunner.h"
#include "ScriptGenerator.h"

#include <ios>
#include <ctime>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <limits>
#include <fstream>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <iterator>
#include <iostream>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace MenuHandler {

    /**
     * Clears the console screen.
     */
    void clearScreen() {
        system("cls");
    }

    /**
     * Waits for the user to press Enter before returning.
     */
    void waitForReturn() {
        std::wcin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::wcout << L"\nPress Enter to return to menu...";
        std::wcin.get();
    }

    /**
     * Prints the PBToolkit ASCII banner header.
     */
    void printBanner() {
        std::wcout << R"(
  ____  ____ _____           _ _    _ _   
 |  _ \| __ )_   _|__   ___ | | | _(_) |_ 
 | |_) |  _ \ | |/ _ \ / _ \| | |/ / | __|
 |  __/| |_) || | (_) | (_) | |   <| | |_ 
 |_|   |____/ |_|\___/ \___/|_|_|\_\_|\__|

)" << std::endl;
    }

    /**
     * Executes the full PBToolkit pipeline:
     *        1) Mirror sync
     *        2) PBL extraction
     *        3) Python post-processing pipeline
     */
    void runFullPipeline() {
        clearScreen();
        printBanner();
        Logger::Info(L"[FULL] Running full pipeline: sync, export, python");

        PblScanner scanner;
        auto pbls = scanner.scan();

        if (pbls.empty()) {
            Logger::Warn(L"[FULL] No PBLs found.");
            waitForReturn();
            return;
        }

        const std::vector<std::wstring> supportedVersions = {
            Config::V65, Config::V7, Config::V8,
            Config::V9, Config::V105, Config::V125
        };

        int totalErrors = 0;
        std::wstringstream globalLogBuffer;

        // Per-version export loop
        for (const auto& version : supportedVersions) {

            std::vector<TargetInfo> versionPbls;
            std::copy_if(
                pbls.begin(), pbls.end(),
                std::back_inserter(versionPbls),
                [&](const TargetInfo& p) { return p.version == version; }
            );

            if (versionPbls.empty()) continue;

            Logger::Info(L"=== Version " + version + L" ===", 1);

            std::size_t total = versionPbls.size();
            std::size_t current = 0;

            for (const auto& pbl : versionPbls) {
                ScriptGenerator generator(pbl.version, pbl.fullPath);
                generator.generateAndRun();

                if (generator.hadError()) {
                    totalErrors++;
                    globalLogBuffer << L"[" << version << L"] "
                        << pbl.fullPath.wstring() << std::endl;
                }

                std::wstring prefix = L"[EXPORT " + version + L"] ";
                Utils::progressBar(current++, total, prefix);
            }

            std::wcout << std::endl;
        }

        // Write export error log (if any)
        if (totalErrors > 0) {
            const fs::path logDir = Utils::toAbsolute(
                L"Resource Files/Extraction/Sources/Logs"
            );
            fs::create_directories(logDir);

            auto now = std::chrono::system_clock::now();
            auto t = std::chrono::system_clock::to_time_t(now);
            tm lt{};
            localtime_s(&lt, &t);

            std::wstringstream logName;
            logName << L"log_" << std::put_time(&lt, L"%Y%m%d_%H%M%S") << L".log";

            fs::path fullLogPath = logDir / logName.str();

            std::wofstream ofs(fullLogPath);
            ofs << L"A total of " << totalErrors
                << L" errors occurred during the PBL export process.\n";
            ofs << globalLogBuffer.str();
            ofs.close();

            Logger::Warn(
                L"[EXPORT] " + std::to_wstring(totalErrors) +
                L" errors during export.\nSee log: " + fullLogPath.wstring(),
                1
            );
        }
        else {
            std::wcout << L"\n";
            Logger::Info(L"[EXPORT] Extraction completed with no errors.");
        }

        std::wcout << L"\n";

        // Python pipeline
        if (!PythonRunner::runFullPipeline()) {
            Logger::Error(L"[PY] Python pipeline failed.");
        }

        Logger::Info(L"[FULL] Pipeline complete. Errors: " +
            std::to_wstring(totalErrors));

        waitForReturn();
    }

    /**
     * Runs only the mirror synchronization step.
     */
    void runMirrorOnly() {
        clearScreen();
        printBanner();
        Logger::Info(L"[SYNC] Syncing mirror only");

        PblScanner scanner;
        scanner.scan();

        Logger::Info(L"[SYNC] Sync complete");
        waitForReturn();
    }

    /**
     * Runs only the PBL extraction step (no sync, no python).
     */
    void runExportOnly() {
        clearScreen();
        printBanner();
        Logger::Info(L"[EXPORT] Starting PBL extraction (no sync, no python)");

        const std::vector<std::wstring> supportedVersions = {
            Config::V65, Config::V7, Config::V8,
            Config::V9, Config::V105, Config::V125
        };

        const fs::path mirrorRoot = Config::MIRROR_ROOT;

        std::vector<TargetInfo> pbls;

        // Load all mirrored PBLs
        for (const auto& v : supportedVersions) {
            fs::path versionRoot = mirrorRoot / v;
            if (!fs::exists(versionRoot)) continue;

            for (const auto& it : fs::recursive_directory_iterator(versionRoot)) {
                if (!it.is_regular_file()) continue;

                const fs::path& p = it.path();
                if (Utils::toLower(p.extension().wstring()) == L".pbl") {
                    TargetInfo info;
                    info.version = v;
                    info.name = p.stem().wstring();
                    info.fullPath = p;
                    pbls.push_back(info);
                }
            }
        }

        if (pbls.empty()) {
            Logger::Warn(L"[EXPORT] No PBLs found in local mirror.");
            waitForReturn();
            return;
        }

        Logger::Info(
            L"[EXPORT] Exporting " + std::to_wstring(pbls.size()) + L" PBLs..."
        );

        int totalErrors = 0;
        std::wstringstream globalLogBuffer;

        // Export loop
        for (const auto& version : supportedVersions) {

            std::vector<TargetInfo> versionPbls;
            std::copy_if(
                pbls.begin(), pbls.end(),
                std::back_inserter(versionPbls),
                [&](const TargetInfo& p) { return p.version == version; }
            );

            if (versionPbls.empty()) continue;

            Logger::Info(L"=== Version " + version + L" ===", 1);

            std::size_t total = versionPbls.size();
            std::size_t current = 0;

            for (const auto& pbl : versionPbls) {
                ScriptGenerator generator(pbl.version, pbl.fullPath);
                generator.generateAndRun();

                if (generator.hadError()) {
                    totalErrors++;
                    globalLogBuffer << L"[" << version << L"] "
                        << pbl.fullPath.wstring() << std::endl;
                }

                std::wstring prefix = L"[PBL " + version + L"] ";
                Utils::progressBar(current++, total, prefix);
            }

            std::wcout << L"\n";
        }

        // Error handling
        if (totalErrors > 0) {
            const fs::path logDir = Config::SOURCES_DIR / "Logs";

            fs::create_directories(logDir);

            auto now = std::chrono::system_clock::now();
            auto t = std::chrono::system_clock::to_time_t(now);
            tm lt{};
            localtime_s(&lt, &t);

            std::wstringstream logName;
            logName << L"log_" << std::put_time(&lt, L"%Y%m%d_%H%M%S") << L".log";

            fs::path fullLogPath = logDir / logName.str();

            std::wofstream ofs(fullLogPath);
            ofs << L"A total of " << totalErrors
                << L" errors occurred during the PBL export process.\n";
            ofs << globalLogBuffer.str();
            ofs.close();

            Logger::Warn(
                L"[EXPORT] " + std::to_wstring(totalErrors) +
                L" errors during export.\nSee log: " + fullLogPath.wstring(), 1
            );
        }
        else {
            Logger::Info(L"[EXPORT] Extraction completed with no errors.");
        }

        waitForReturn();
    }

    /**
     * Main interactive menu loop. Displays options and dispatches actions.
     */
    void showMainMenu() {
        while (true) {
            clearScreen();
            printBanner();

            std::wcout << L"1 - Run full pipeline (sync + export + python)\n";
            std::wcout << L"2 - Sync mirror\n";
            std::wcout << L"3 - Extract PBLs\n";
            std::wcout << L"4 - Python Menu\n";
            std::wcout << L"0 - Exit\n\n";
            std::wcout << L"> Select option: ";

            int choice;
            std::wcin >> choice;

            switch (choice) {
            case 1: runFullPipeline(); break;
            case 2: runMirrorOnly();   break;
            case 3: runExportOnly();   break;
            case 4: PythonRunner::menu(); break;
            case 0: return;
            default:
                Logger::Warn(L"Invalid option");
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }
}