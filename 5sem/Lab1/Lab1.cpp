#include <windows.h>
#include <tchar.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <ctime>

void OutputConsole(const std::wstring& message) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    
    if (hConsole == INVALID_HANDLE_VALUE) {
        std::cerr << "Некорректный дескриптор консоли.\n";
    }

    DWORD lpCharsWritten;

    if (!WriteConsole(hConsole, message.c_str(), message.size(), &lpCharsWritten, NULL)) {
        std::cerr << "Ошибка записи в консоль\n";
    }
}

class ProgramSettings {
public:
    std::wstring lpApplicationName;
    std::wstring time;
    std::wstring dayOfMonth;
    std::wstring month;
    std::wstring dayOfWeek;
    std::wstring lpCommandLine;

    ProgramSettings(std::wstring appName, std::wstring time, std::wstring cli, 
        std::wstring dayOfMonth = L"*", std::wstring month = L"*", std::wstring dayOfWeek = L"*")
        : lpApplicationName(appName), time(time), lpCommandLine(cli), 
        dayOfMonth(dayOfMonth), month(month), dayOfWeek(dayOfWeek) {}

    void run(const std::wstring& time = L"") {
        STARTUPINFO si;
        PROCESS_INFORMATION pi;

        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));

        std::wstring commandLine = lpApplicationName + L" " + lpCommandLine;

        if (GetFileAttributes(lpApplicationName.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::wcerr << time << L" Файл не найден: " << lpApplicationName << L"\n";
            return;
        }

        if (!time.empty())
            OutputConsole(time + L" ");

        OutputConsole(L"Запуск процесса " + lpApplicationName + L"...\n");

        if (!CreateProcess(NULL, &commandLine[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        {
            OutputConsole(L"Запуск процесса " + lpApplicationName + L" неуспешен (" + std::to_wstring(GetLastError()) + L")\n");
            return;
        }

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
};

std::wstring FormatDate(const std::wstring& date_str, int l_border, int r_border) {
    if (date_str == L"*")
        return date_str;

    int date = _wtoi(date_str.c_str());
    if (date < l_border || date > r_border)
        throw std::invalid_argument("Неверный формат даты");

    return std::to_wstring(date);
}

std::wstring FormatTime(const std::wstring& time_str) {
    size_t colon = time_str.find(':');
    if (colon == std::wstring::npos)
        throw std::invalid_argument("Неверный формат времени");

    std::wstring hours_str = time_str.substr(0, colon);
    std::wstring minutes_str = time_str.substr(colon + 1);

    int hours = _wtoi(hours_str.c_str());
    int minutes = _wtoi(minutes_str.c_str());

    if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59)
        throw std::invalid_argument("Неверный формат времени");

    return std::to_wstring(hours) + L":" + std::to_wstring(minutes);
}

std::vector<ProgramSettings> GetSchedule(std::wstring filename, std::vector<ProgramSettings>& schedule)
{
    std::wifstream fin(filename);

    if (fin.is_open())
    {
        std::wstring buff;
        while (std::getline(fin, buff))
        {
            std::wistringstream wiss(buff);
            std::wstring minute, hour, dayOfMonth, month, dayOfWeek, appName, cli, time;
            if (!(wiss >> minute >> hour >> dayOfMonth >> month >> dayOfWeek >> appName))
            {
                std::cerr << "Ошибка: неверная конфигурация. Требуемый формат \
конфигурации: <минута> <час> <день>('*', если не требуется) <месяц>('*', если не требуется) \
<день_недели>('*', если не требуется) <путь/к/файлу> <параметры_запуска>" << '\n';
                continue;
            }
            std:getline(wiss, cli);
            time = hour + L":" + minute;
            try {
                time = FormatTime(time);
                dayOfMonth = FormatDate(dayOfMonth, 1, 31);
                month = FormatDate(month, 1, 12);
                dayOfWeek = FormatDate(dayOfWeek, 1, 7);
            }
            catch (const std::invalid_argument& exc) {
                std::cerr << "Ошибка преобразования: " << exc.what() << '\n';
                continue;
            }
            if (appName.empty())
            {
                std::cerr << "Ошибка: неверный путь к файлу" << '\n';
                continue;
            }
            ProgramSettings progSet(appName, time, cli, dayOfMonth, month, dayOfWeek);
            schedule.push_back(progSet);
        }
    }
    else {
        std::wcerr << L"Не удалось открыть файл: " << filename << '\n';
    }

    fin.close();

    return schedule;
}

void RunSchedule(std::vector<ProgramSettings> schedule) 
{
    SYSTEMTIME lt;

    OutputConsole(L"Планировщик запущен\n");

    while (true) {
        GetLocalTime(&lt);
        std::wstring now = std::to_wstring(lt.wHour) + L":" + std::to_wstring(lt.wMinute);
        std::wstring dayOfMonth = std::to_wstring(lt.wDay);
        std::wstring month = std::to_wstring(lt.wMonth);
        std::wstring dayOfWeek = std::to_wstring(lt.wDayOfWeek) != L"0" ? std::to_wstring(lt.wDayOfWeek) : L"7";
        if (lt.wSecond == 0) {
            for (auto it = schedule.begin(); it != schedule.end(); ++it) {
                if (it->time == now 
                    && (it->dayOfMonth == L"*" || it->dayOfMonth == dayOfMonth) 
                    && (it->month == L"*" || it->month == month) 
                    && (it->dayOfWeek == L"*" || it->dayOfWeek == dayOfWeek) || 1) {
                    it->run(now);
                }
            }
        }
        std::wstring now_sec = std::to_wstring(lt.wHour) + L":" + 
            std::to_wstring(lt.wMinute) + L":" + std::to_wstring(lt.wSecond);
        OutputConsole(now_sec + L"\r");
        Sleep(1000);
    }
}

void _tmain(int argc, TCHAR* argv[])
{
    setlocale(LC_ALL, "ru");

    std::vector<std::wstring> files = {
        L"schedule.txt",
    };

    std::vector<ProgramSettings> schedule;

    for (auto file : files)
        GetSchedule(file, schedule);

    if (schedule.empty())
    {
        OutputConsole(L"Ошибка: записи не найдены");
        return;
    }
    else
    {
        OutputConsole(L"Найдено записей: " + std::to_wstring(schedule.size()) + L"\n");
    }

    RunSchedule(schedule);
}
