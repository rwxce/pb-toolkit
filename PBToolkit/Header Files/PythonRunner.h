#pragma once
#include <string>
#include <vector>

/**
 * Provides utilities to run Python scripts from the application.
 */
class PythonRunner
{
public:
    /**
     * Runs a Python script and returns whether it exited successfully.
     * @param scriptName The Python script file to execute.
     * @param args Optional list of arguments passed to the script.
     * @return true if the process completed successfully; false otherwise.
     */
    static bool runScript(
        const std::wstring& scriptName,
        const std::vector<std::wstring>& args = {}
    );

    /**
     * Executes the complete post-processing pipeline.
     * @return true if the full pipeline succeeds; false on failure.
     */
    static bool runFullPipeline();

    /**
     * Displays the interactive Python menu, allowing the user to run standalone scripts, list available scripts, or rebuild the virtual environment.
     */
    static void menu();

    /**
     * Shows a numeric submenu containing all Python scripts and executes the one selected by the user.
     * @return true if the selected script completes successfully; false otherwise.
     */
    static bool runSingleScript();

    /**
     * Prints the list of available Python scripts inside PY_SCRIPTS_DIR.
     */
    static void printScripts();

    /**
     * Deletes the existing Python virtual environment so that a clean one can be created on the next pipeline run.
     * @return true if the environment is removed successfully; false otherwise.
     */
    static bool rebuildVenv();
};