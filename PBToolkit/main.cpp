#include <io.h>
#include <cstdio>
#include <fcntl.h>
#include <windows.h>

#include "MenuHandler.h"

int main()
{
    // Set UTF-8 console mode
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    (void)_setmode(_fileno(stdout), _O_U8TEXT);
    (void)_setmode(_fileno(stderr), _O_U8TEXT);

    // Set window title
    SetConsoleTitle(L"PBToolkit");

    // Launch main interactive menu
    MenuHandler::showMainMenu();

    return 0;
}