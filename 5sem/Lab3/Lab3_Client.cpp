#include <windows.h>
#include <iostream>
#include <conio.h>

#define BUFFER_SIZE 512

const wchar_t* LP_PIPENAME = L"\\\\.\\pipe\\pipename";

int wmain() {
    setlocale(LC_ALL, "ru");
    HANDLE hPipe;
    const wchar_t* message = L"Сообщение от клиента.";

    hPipe = CreateFileW(
        LP_PIPENAME,
        GENERIC_READ |
        GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    if (hPipe == INVALID_HANDLE_VALUE) {
        std::wcerr << L"Ошибка подключения к каналу: " << GetLastError() << L'\n';
        return 1;
    }

    std::wcout << L"Отправка серверу сообщения: " << message << L'\n';

    DWORD bytesWritten;
    if (!WriteFile(hPipe, message, (wcslen(message) + 1) * sizeof(wchar_t), &bytesWritten, NULL)) {
        std::wcerr << L"Ошибка отправки сообщения: " << GetLastError() << L'\n';
        return 1;
    }

    std::wcout << L"Сообщение отправлено. Ожидание ответа от сервера...\n";

    DWORD bytesRead;
    wchar_t* responseMessage = new wchar_t[BUFFER_SIZE];

    if (!ReadFile(hPipe, responseMessage, BUFFER_SIZE * sizeof(wchar_t), &bytesRead, NULL)) {
        std::wcerr << L"Ошибка чтения ответа: " << GetLastError() << L'\n';
        delete[] responseMessage;
        return 1;
    }

    std::wcout << L"Ответ от сервера: " << responseMessage << L'\n';

    std::wcout << L"Нажмите любую клавишу для выхода...";
    _getch();

    CloseHandle(hPipe);
    delete[] responseMessage;

    return 0;
}