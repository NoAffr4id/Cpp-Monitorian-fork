#include <windows.h>
#include <physicalmonitorenumerationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <lowlevelmonitorconfigurationapi.h>

#include <iostream>
#include <vector>
#include <string>
#include <io.h>
#include <fcntl.h>

#pragma comment(lib, "Dxva2.lib")
#pragma comment(lib, "User32.lib")

struct MonitorEntry {
    HMONITOR hMonitor;
    HANDLE   hPhysical;
    std::wstring description;
};

static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam)
{
    auto* monitors = reinterpret_cast<std::vector<HMONITOR>*>(lParam);
    monitors->push_back(hMonitor);
    return TRUE;
}

static std::vector<MonitorEntry> EnumeratePhysicalMonitors()
{
    std::vector<MonitorEntry> result;
    std::vector<HMONITOR> hMonitors;

    if (!EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc,
        reinterpret_cast<LPARAM>(&hMonitors))) {
        std::wcerr << L"EnumDisplayMonitors failed, err=" << GetLastError() << L"\n";
        return result;
    }

    for (HMONITOR hMon : hMonitors) {
        DWORD numPhysical = 0;
        if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hMon, &numPhysical) || numPhysical == 0) {
            continue;
        }

        std::vector<PHYSICAL_MONITOR> physMonitors(numPhysical);
        if (!GetPhysicalMonitorsFromHMONITOR(hMon, numPhysical, physMonitors.data())) {
            std::wcerr << L"GetPhysicalMonitorsFromHMONITOR failed, err=" << GetLastError() << L"\n";
            continue;
        }

        for (auto& pm : physMonitors) {
            MonitorEntry entry;
            entry.hMonitor = hMon;
            entry.hPhysical = pm.hPhysicalMonitor;
            entry.description = pm.szPhysicalMonitorDescription;
            result.push_back(entry);
        }
    }
    return result;
}

static void FreeMonitors(std::vector<MonitorEntry>& monitors)
{
    for (auto& m : monitors) {
        DestroyPhysicalMonitor(m.hPhysical);
    }
}

static void PrintUsage()
{
    std::wcout <<
        L"MonitorianLite - управление яркостью внешних мониторов (DDC/CI)\n\n"
        L"Использование:\n"
        L"  MonitorianLite.exe list\n"
        L"  MonitorianLite.exe get <index>\n"
        L"  MonitorianLite.exe set <index> <value 0-100>\n";
}

static bool GetBrightnessInfo(HANDLE hPhysical, DWORD& minVal, DWORD& curVal, DWORD& maxVal)
{
    return GetMonitorBrightness(hPhysical, &minVal, &curVal, &maxVal) != FALSE;
}

int wmain(int argc, wchar_t* argv[])
{
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);
    SetConsoleOutputCP(CP_UTF8);

    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    std::wstring cmd = argv[1];
    auto monitors = EnumeratePhysicalMonitors();

    if (monitors.empty()) {
        std::wcerr << L"Не найдено ни одного монитора с поддержкой DDC/CI.\n"
            L"Убедитесь, что DDC/CI включён в OSD-меню монитора.\n";
        return 2;
    }

    if (cmd == L"list") {
        std::wcout << L"Найдено мониторов: " << monitors.size() << L"\n\n";
        for (size_t i = 0; i < monitors.size(); ++i) {
            DWORD mn = 0, cur = 0, mx = 0;
            bool ok = GetBrightnessInfo(monitors[i].hPhysical, mn, cur, mx);
            std::wcout << L"[" << i << L"] " << monitors[i].description;
            if (ok) {
                std::wcout << L"  brightness=" << cur << L" (min=" << mn << L", max=" << mx << L")";
            }
            else {
                std::wcout << L"  (яркость недоступна: err=" << GetLastError() << L")";
            }
            std::wcout << L"\n";
        }
    }
    else if (cmd == L"get") {
        if (argc < 3) { PrintUsage(); FreeMonitors(monitors); return 1; }
        size_t idx = static_cast<size_t>(_wtoi(argv[2]));
        if (idx >= monitors.size()) {
            std::wcerr << L"Неверный индекс монитора.\n";
            FreeMonitors(monitors);
            return 3;
        }
        DWORD mn = 0, cur = 0, mx = 0;
        if (GetBrightnessInfo(monitors[idx].hPhysical, mn, cur, mx)) {
            std::wcout << cur << L" (min=" << mn << L", max=" << mx << L")\n";
        }
        else {
            std::wcerr << L"Ошибка получения яркости, err=" << GetLastError() << L"\n";
            FreeMonitors(monitors);
            return 4;
        }
    }
    else if (cmd == L"set") {
        if (argc < 4) { PrintUsage(); FreeMonitors(monitors); return 1; }
        size_t idx = static_cast<size_t>(_wtoi(argv[2]));
        int value = _wtoi(argv[3]);
        if (value < 0)   value = 0;
        if (value > 100) value = 100;

        if (idx >= monitors.size()) {
            std::wcerr << L"Неверный индекс монитора.\n";
            FreeMonitors(monitors);
            return 3;
        }

        DWORD mn = 0, cur = 0, mx = 0;
        if (!GetBrightnessInfo(monitors[idx].hPhysical, mn, cur, mx)) {
            std::wcerr << L"Не удалось прочитать диапазон яркости, err=" << GetLastError() << L"\n";
            FreeMonitors(monitors);
            return 4;
        }

        DWORD target = mn + static_cast<DWORD>((mx - mn) * (value / 100.0));

        if (SetMonitorBrightness(monitors[idx].hPhysical, target)) {
            std::wcout << L"OK: установлено " << value << L"% (raw=" << target << L")\n";
        }
        else {
            std::wcerr << L"Ошибка установки яркости, err=" << GetLastError() << L"\n";
            FreeMonitors(monitors);
            return 5;
        }
    }
    else {
        PrintUsage();
        FreeMonitors(monitors);
        return 1;
    }

    FreeMonitors(monitors);
    return 0;
}
