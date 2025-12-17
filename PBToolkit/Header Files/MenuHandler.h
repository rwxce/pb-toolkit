#pragma once

/**
 * Main menu handler responsible for user interaction.
 * Provides access to the core PBToolkit operations, including the full pipeline,
 * mirroring, PBL export, and Python script execution.
 */
namespace MenuHandler {
    // Print ASCII banner
    void printBanner();

    // Main menu loop controller
    void showMainMenu();

    // Operations corresponding to user options
    void runFullPipeline();
    void runMirrorOnly();
    void runExportOnly();

    // Utilities
    void clearScreen();
    void waitForReturn();
}