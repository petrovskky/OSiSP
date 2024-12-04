#include <iostream>
#include <windows.h>
#include <vector>
#include <string>
#include <random>
#include <chrono>

HANDLE* rMutexes;
HANDLE* dataMutexes;
HANDLE* noReadSemaphores;
HANDLE* writersQueueSemaphores;
int* rCounts;
LONG* wRequests;
bool running = true;

HANDLE consoleMutex;
HANDLE statsMutex;

std::vector<int*> buffers;
int bufferCount{};
int bufferSize{};

int rTimeout{};
int wTimeout{};

int rDelay = 1000;
int wDelay = 1500;

LONG successReadCount = 0;
LONG failReadCount = 0;
LONG successWriteCount = 0;
LONG failWriteCount = 0;

const int WORK_TIME = 10000;

void LogResults() {
    HANDLE hFile = CreateFileA(
        "simulation_results.txt",
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Не удалось открыть файл для записи." << std::endl;
        return;
    }

    std::string logData = "Результаты моделирования\n" +
        std::string("Читатели\n") +
        "  Количество попыток чтения " + std::to_string(successReadCount + failReadCount) +
        "\n  Успешные обращения: " + std::to_string(successReadCount) +
        "\n  Неуспешные обращения: " + std::to_string(failReadCount) +
        "\nПисатели\n" +
        "  Количество попыток записи " + std::to_string(successWriteCount + failWriteCount) +
        "\n  Успешные обращения: " + std::to_string(successWriteCount) +
        "\n  Неуспешные обращения: " + std::to_string(failWriteCount) +
        "\nКоэффициент успешного чтения и записи\n" +
        "  Чтение: " + std::to_string((int)((double)successReadCount / (double)(successReadCount + failReadCount) * 100.0)) +
        "%\n  Запись: " + std::to_string((int)((double)successWriteCount / (double)(successWriteCount + failWriteCount) * 100.0)) + "%\n";

    DWORD bytesWritten;
    if (!WriteFile(hFile, logData.c_str(), logData.size(), &bytesWritten, NULL)) {
        std::cerr << "Ошибка записи в файл." << std::endl;
    }

    CloseHandle(hFile);
}

int SafeInputInt(const std::string& message) {
    int value;
    while (true) {
        std::cout << message;
        std::cin >> value;

        if (std::cin.fail() || value <= 0) {
            std::cin.clear();
            std::cin.ignore(10000, '\n');
            std::cout << "Ошибка: введите положительное целое число.\n";
        }
        else {
            return value;
        }
    }
}

int SafeInputTimeout(const std::string& message) {
    int value;
    while (true) {
        std::cout << message;
        std::cin >> value;

        if (std::cin.fail() || value < 0) {
            std::cin.clear();
            std::cin.ignore(10000, '\n');
            std::cout << "Ошибка: введите неотрицательное целое число.\n";
        }
        else {
            return value;
        }
    }
}

DWORD WINAPI ReaderThread(LPVOID lpParam) {
    int id = (int)lpParam;

    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<int> bufferDist(0, bufferCount - 1);
    std::uniform_int_distribution<int> delayDist(0, 1000);

    SYSTEMTIME st;

    while (running) {
        int bufferIndex = bufferDist(generator);

        if (WaitForSingleObject(writersQueueSemaphores[bufferIndex], 0) == WAIT_OBJECT_0) {
            InterlockedIncrement(&failReadCount);

            Sleep(delayDist(generator) + rDelay);
            continue;
        }

        WaitForSingleObject(rMutexes[bufferIndex], INFINITE);
        rCounts[bufferIndex]++;
        if (rCounts[bufferIndex] == 1) {
            if (WaitForSingleObject(noReadSemaphores[bufferIndex], rTimeout) == WAIT_TIMEOUT) {
                InterlockedIncrement(&failReadCount);

                rCounts[bufferIndex]--;
                ReleaseMutex(rMutexes[bufferIndex]);

                continue;
            }
        }
        ReleaseMutex(rMutexes[bufferIndex]);


        WaitForSingleObject(consoleMutex, INFINITE);
        GetLocalTime(&st);
        std::cout << "[" << st.wHour << ":" << st.wMinute << ":" << st.wSecond << "." << st.wMilliseconds << "] Читатель " << id << " читает книгу " << bufferIndex << ": \n";

        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < bufferSize; ++i) {
            std::cout << buffers[bufferIndex][i] << ' ';
        }
        std::cout << '\n';
        Sleep(100);
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> duration = end - start;

        std::cout << "Читатель " << id << " закончил чтение книги " << bufferIndex
            << " за " << duration.count() << " мс.\n";
        ReleaseMutex(consoleMutex);

        InterlockedIncrement(&successReadCount);

        WaitForSingleObject(rMutexes[bufferIndex], INFINITE);
        rCounts[bufferIndex]--;
        if (rCounts[bufferIndex] == 0) {
            ReleaseSemaphore(noReadSemaphores[bufferIndex], 1, NULL);
        }
        ReleaseMutex(rMutexes[bufferIndex]);

        Sleep(delayDist(generator) + rDelay);
    }
    return 0;
}

DWORD WINAPI WriterThread(LPVOID lpParam) {
    int id = (int)lpParam;

    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<int> bufferDist(0, bufferCount - 1);
    std::uniform_int_distribution<int> valueDist(0, 100);
    std::uniform_int_distribution<int> delayDist(0, 1000);

    SYSTEMTIME st;

    while (running) {
        int bufferIndex = bufferDist(generator);

        InterlockedIncrement(&wRequests[bufferIndex]);
        ReleaseSemaphore(writersQueueSemaphores[bufferIndex], 1, NULL);

        if (WaitForSingleObject(noReadSemaphores[bufferIndex], wTimeout) == WAIT_TIMEOUT) {
            InterlockedIncrement(&failWriteCount);
            InterlockedDecrement(&wRequests[bufferIndex]);
            WaitForSingleObject(writersQueueSemaphores[bufferIndex], 0);
            continue;
        }

        WaitForSingleObject(dataMutexes[bufferIndex], INFINITE);
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < bufferSize; ++i) {
            buffers[bufferIndex][i] = valueDist(generator);
        }
        Sleep(150);
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> duration = end - start;
        ReleaseMutex(dataMutexes[bufferIndex]);

        InterlockedIncrement(&successWriteCount);

        WaitForSingleObject(consoleMutex, INFINITE);
        GetLocalTime(&st);
        std::cout << "[" << st.wHour << ":" << st.wMinute << ":" << st.wSecond << "." << st.wMilliseconds << "] Писатель " << id << " записывает в книгу " << bufferIndex << ": \n";
        for (int i = 0; i < bufferSize; ++i) {
            std::cout << buffers[bufferIndex][i] << ' ';
        }
        std::cout << '\n';

        std::cout << "Писатель " << id << " закончил запись в книгу " << bufferIndex
            << " за " << duration.count() << " мс.\n";
        ReleaseMutex(consoleMutex);

        InterlockedDecrement(&wRequests[bufferIndex]);
        if (wRequests[bufferIndex] == 0) {
            WaitForSingleObject(writersQueueSemaphores[bufferIndex], 0);
        }
        ReleaseSemaphore(noReadSemaphores[bufferIndex], 1, NULL);

        Sleep(delayDist(generator) + wDelay);
    }
    return 0;
}

int main() {
    setlocale(LC_ALL, "ru");
    std::vector<HANDLE> rThreads, wThreads;

    int rNum = SafeInputInt("Введите количество читателей: ");
    int wNum = SafeInputInt("Введите количество писателей: ");
    bufferCount = SafeInputInt("Введите количество книг: ");
    rTimeout = SafeInputTimeout("Введите тайм-аут для читателя: ");
    wTimeout = SafeInputTimeout("Введите тайм-аут для писателя: ");
    bufferSize = SafeInputInt("Введите размер книги: ");

    rMutexes = new HANDLE[bufferCount];
    dataMutexes = new HANDLE[bufferCount];
    noReadSemaphores = new HANDLE[bufferCount];
    writersQueueSemaphores = new HANDLE[bufferCount];
    rCounts = new int[bufferCount] {};
    wRequests = new LONG[bufferCount] {};

    for (int i = 0; i < bufferCount; ++i) {
        rMutexes[i] = CreateMutex(NULL, FALSE, NULL);
        dataMutexes[i] = CreateMutex(NULL, FALSE, NULL);
        noReadSemaphores[i] = CreateSemaphore(NULL, 1, 1, NULL);
        writersQueueSemaphores[i] = CreateSemaphore(NULL, 0, 1, NULL);
        buffers.push_back(new int[bufferSize]());
    }

    consoleMutex = CreateMutex(NULL, FALSE, NULL);
    statsMutex = CreateMutex(NULL, FALSE, NULL);

    for (int i = 0; i < rNum; ++i) {
        HANDLE hThread = CreateThread(NULL, 0, ReaderThread, (void*)i, 0, NULL);
        rThreads.push_back(hThread);
    }

    for (int i = 0; i < wNum; ++i) {
        HANDLE hThread = CreateThread(NULL, 0, WriterThread, (void*)i, 0, NULL);
        wThreads.push_back(hThread);
    }

    Sleep(WORK_TIME);
    running = false;
    WaitForSingleObject(consoleMutex, INFINITE);
    std::cout << "Сигнал завершения программы...\n";
    ReleaseMutex(consoleMutex);

    WaitForMultipleObjects(rNum, rThreads.data(), TRUE, INFINITE);
    WaitForMultipleObjects(wNum, wThreads.data(), TRUE, INFINITE);

    for (HANDLE hThread : rThreads)
        CloseHandle(hThread);

    for (HANDLE hThread : wThreads)
        CloseHandle(hThread);

    for (int i = 0; i < bufferCount; ++i) {
        CloseHandle(rMutexes[i]);
        CloseHandle(dataMutexes[i]);
        CloseHandle(noReadSemaphores[i]);
        CloseHandle(writersQueueSemaphores[i]);
        delete[] buffers[i];
    }

    CloseHandle(consoleMutex);
    CloseHandle(statsMutex);

    delete[] rMutexes;
    delete[] dataMutexes;
    delete[] noReadSemaphores;
    delete[] writersQueueSemaphores;
    delete[] rCounts;
    delete[] wRequests;

    LogResults();

    return 0;
}
