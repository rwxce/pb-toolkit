#include "Utils.h"
#include "Logger.h"
#include "Config.h"
#include "ScriptGenerator.h"

#include <string>
#include <sstream>
#include <Windows.h>
#include <filesystem>

namespace fs = std::filesystem;

/**
 * Constructor. Initializes internal paths based on version and PBL input.
 * @param version PowerBuilder version
 * @param pblPath Full path to the input .PBL file
 */
ScriptGenerator::ScriptGenerator(const std::wstring& version, const fs::path& pblPath)
    : m_version(version), m_pblPath(pblPath)
{
    m_outputDir = Config::SOURCES_DIR / m_version / pblPath.stem();
    fs::create_directories(m_outputDir);
}

/**
 * Executes the PblDump utility to extract all objects from the specified PBL file.
 * @return true if successful, false otherwise
 */
bool ScriptGenerator::generateAndRun()
{
    m_hadError = false;

    // Resolve full path to PblDump.exe
    const fs::path pbldumpExe = Config::PBLDUMP_EXE;
    if (!fs::exists(pbldumpExe)) {
        Logger::Error(L"[PBLEXPORT] Cannot find PblDump.exe");
        m_hadError = true;
        return false;
    }

    // Build command line for execution
    std::wstringstream cmd;
    cmd << L"\"" << pbldumpExe.wstring() << L"\" "
        << L"-esu "
        << L"\"" << m_pblPath.wstring() << L"\" "
        << L"*.*";

    fs::create_directories(m_outputDir);
    std::wstring workingDir = m_outputDir.wstring();

    // Redirect output to NUL to suppress stdout/stderr
    HANDLE hNull = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hNull == INVALID_HANDLE_VALUE) {
        Logger::Error(L"[PBLEXPORT] Unable to open NUL for output redirection");
        m_hadError = true;
        return false;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hNull;
    si.hStdError = hNull;

    PROCESS_INFORMATION pi{};
    std::wstring commandLine = cmd.str();
    BOOL ok = CreateProcessW(
        nullptr,
        &commandLine[0],
        nullptr, nullptr, TRUE, // inherit handles
        0,
        nullptr,
        workingDir.c_str(), // set working directory
        &si,
        &pi
    );

    CloseHandle(hNull);

    if (!ok) {
        Logger::Error(L"[PBLEXPORT] Failed to launch PblDump process");
        m_hadError = true;
        return false;
    }

    DWORD timeoutMs = 10000;  // 10 seconds

    DWORD result = WaitForSingleObject(pi.hProcess, timeoutMs);
    if (result == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        Logger::Error(L"[PBLEXPORT] PblDump execution timed out with: " + m_pblPath.wstring());
        m_hadError = true;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return false;
    }

    // Wait for the process to finish and capture exit code
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exitCode != 0) {
        m_hadError = true;
        return false;
    }

    return true;
}