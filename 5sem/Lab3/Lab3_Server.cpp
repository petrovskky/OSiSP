#include <iostream>
#include <windows.h>
#include <strsafe.h> 
#include <tchar.h>

#define FILE_NAME_MAX 256
#define BUFFER_SIZE 512
#define FILE_SIZE_MAX 2000

const wchar_t* LP_PIPENAME = L"\\\\.\\pipe\\pipename";
const wchar_t* FILE_NAME = L"logfile.txt";

HANDLE hFile;
CRITICAL_SECTION csFile;

void ArchiveFile() {
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize) || FILE_SIZE_MAX >= fileSize.QuadPart) {
        return;
    }

    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t archiveFileName[FILE_NAME_MAX];
    StringCchPrintfW(
        archiveFileName,
        FILE_NAME_MAX,
        L"log_%04d%02d%02d_%02d%02d%02d.log",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond
    );

    CloseHandle(hFile);
    MoveFileW(FILE_NAME, archiveFileName);

    hFile = CreateFileW(FILE_NAME, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

void LogMessage(const wchar_t* logString) {
    EnterCriticalSection(&csFile);

    ArchiveFile();

    DWORD bytesWritten;
    SetFilePointer(hFile, 0, NULL, FILE_END);
    WriteFile(hFile, logString, wcslen(logString) * sizeof(wchar_t), &bytesWritten, NULL);

    LeaveCriticalSection(&csFile);
}

DWORD WINAPI ThreadProc(LPVOID lpParam) {
    if (!lpParam) {
        std::wcout << L"Ошибка: нулевое значение параметра, переданного потоку\n";
        return 1;
    }

    HANDLE hPipe = (HANDLE)lpParam;

    HANDLE hHeap = GetProcessHeap();
    wchar_t* requestMessage = (wchar_t*)HeapAlloc(hHeap, 0, BUFFER_SIZE * sizeof(wchar_t));
    wchar_t* responseMessage = (wchar_t*)HeapAlloc(hHeap, 0, BUFFER_SIZE * sizeof(wchar_t));

    if (!requestMessage || !responseMessage) {
        std::wcout << L"Ошибка выделения памяти для потока\n";
        if (requestMessage)
            HeapFree(hHeap, 0, requestMessage);
        if (responseMessage)
            HeapFree(hHeap, 0, responseMessage);
        return 1;
    }

    DWORD clientPID = 0;
    GetNamedPipeClientProcessId(hPipe, &clientPID);

    std::wcout << L"Клиент подключен [PID=" << clientPID << L"]\n";

    DWORD bytesRead;

    while (true) {
        if (!ReadFile(hPipe, requestMessage, BUFFER_SIZE * sizeof(wchar_t), &bytesRead, NULL) || !bytesRead) {
            DWORD errorCode = GetLastError();
            if (errorCode == ERROR_BROKEN_PIPE)
                std::wcout << L"Клиент отключен [PID=" << clientPID << L"]\n";
            else
                std::wcout << L"Ошибка чтения файла: " << errorCode << L'\n';
            break;
        }

        SYSTEMTIME st;
        GetLocalTime(&st);

        wchar_t logString[BUFFER_SIZE];
        StringCchPrintfW(logString, BUFFER_SIZE, L"[%04d-%02d-%02d %02d:%02d:%02d] %d: %s\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, clientPID, requestMessage);

        std::wcout << logString;

        LogMessage(logString);

        DWORD bytesWritten;

        if (FAILED(StringCchCopyW(responseMessage, BUFFER_SIZE, L"Сообщение получено успешно."))) {
            responseMessage[0] = 0;
        }

        WriteFile(hPipe, responseMessage, (wcslen(responseMessage) + 1) * sizeof(wchar_t), &bytesWritten, NULL);
    }

    FlushFileBuffers(hPipe);
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);

    HeapFree(hHeap, 0, requestMessage);
    HeapFree(hHeap, 0, responseMessage);

    return 0;
}

int wmain() {
    setlocale(LC_ALL, "ru");
    InitializeCriticalSection(&csFile);

    std::wcout << L"Сервер запущен\n";

    hFile = CreateFileW(
        FILE_NAME,
        GENERIC_WRITE,
        0,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        std::wcout << L"Ошибка открытия/создания файла: " << GetLastError() << L'\n';
        return 1;
    }

    while (true) {
        HANDLE hPipe = CreateNamedPipeW(
            LP_PIPENAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            BUFFER_SIZE,
            BUFFER_SIZE,
            0,
            NULL
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            std::wcout << L"Ошибка создания именованного канала:" << GetLastError() << L'\n';
            return 1;
        }

        if (ConnectNamedPipe(hPipe, NULL)) {
            DWORD threadId;
            HANDLE hThread = CreateThread(NULL, 0, &ThreadProc, (LPVOID)hPipe, NULL, &threadId);

            if (hThread == NULL) {
                std::wcerr << L"Ошибка создания потока: " << GetLastError() << L'\n';
                return 1;
            }

            CloseHandle(hThread);
        }
        else {
            CloseHandle(hPipe);
        }
    }

    CloseHandle(hFile);
    DeleteCriticalSection(&csFile);

    return 0;
}