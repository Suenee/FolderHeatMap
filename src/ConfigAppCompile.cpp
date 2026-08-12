// Build wrapper for the settings application.
// Windows defines IDC_HELP as a predefined cursor resource macro. ConfigApp.cpp
// also uses IDC_HELP as a control ID, so include the Windows headers first,
// remove that macro, and then compile the actual implementation.
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <tlhelp32.h>

#ifdef IDC_HELP
#undef IDC_HELP
#endif

#include "ConfigApp.cpp"
