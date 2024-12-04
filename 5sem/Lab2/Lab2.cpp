#include <windows.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>

#define FILE_SIZE 1024 * 100
#define THREADS_NUM 8
#define TESTS_NUM 3

const char* FILE_NAME = "data.txt";

void InitFile() {
    HANDLE hFile = CreateFileA(
        FILE_NAME,
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Create file error: " << GetLastError() << '\n';
        CloseHandle(hFile);
        return;
    }

    std::vector<char> data;
    for (int i = 0; i < FILE_SIZE; ++i)
        data.push_back(rand() % 10 + '0');

    DWORD bytesWritten;

    if (!WriteFile(hFile, data.data(), FILE_SIZE, &bytesWritten, NULL)) {
        std::cerr << "Write file error: " << GetLastError() << '\n';
    }

    CloseHandle(hFile);
}

struct Data {
    char* buffer;
    size_t start;
    size_t end;
};

DWORD WINAPI ThreadProc(LPVOID lpParam) {
    Data* data = (Data*)(lpParam);
    char* buffer = data->buffer;
    size_t start = data->start;
    size_t end = data->end;

    for (size_t i = start; i < end; ++i) {
        for (size_t j = i; j > start && buffer[j] < buffer[j - 1]; --j) {
            if (buffer[j] < buffer[j - 1]) {
                int buff = buffer[j];
                buffer[j] = buffer[j - 1];
                buffer[j - 1] = buff;
            }
        }
    }

    return 0;
}

void Merge(char*& buffer, size_t start, size_t mid, size_t end) {
    size_t leftSize = mid - start;
    size_t rightSize = end - mid;

    char* left = new char[leftSize];
    char* right = new char[rightSize];

    for (size_t i = 0; i < leftSize; ++i) {
        left[i] = buffer[start + i];
    }
    for (size_t i = 0; i < rightSize; ++i) {
        right[i] = buffer[mid + i];
    }

    size_t i = 0, j = 0, k = start;

    while (i < leftSize && j < rightSize) {
        if (left[i] <= right[j]) {
            buffer[k++] = left[i++];
        }
        else {
            buffer[k++] = right[j++];
        }
    }

    while (i < leftSize) {
        buffer[k++] = left[i++];
    }

    while (j < rightSize) {
        buffer[k++] = right[j++];
    }

    delete[] left;
    delete[] right;
}

void Sort(char*& buffer, size_t start, size_t end) {
    for (size_t i = start; i < end; ++i) {
        for (size_t j = i; j > start && buffer[j] < buffer[j - 1]; --j) {
            if (buffer[j] < buffer[j - 1]) {
                int buff = buffer[j];
                buffer[j] = buffer[j - 1];
                buffer[j - 1] = buff;
            }
        }
    }
}

void SortThreads(char*& buffer, size_t start, size_t end) {
    HANDLE threads[THREADS_NUM];
    Data data[THREADS_NUM];
    size_t partSize = FILE_SIZE / THREADS_NUM;

    for (int i = 0; i < THREADS_NUM; ++i) {
        data[i].buffer = buffer;
        data[i].start = i * partSize;
        data[i].end = (i == THREADS_NUM - 1) ? FILE_SIZE : (i + 1) * partSize;

        threads[i] = CreateThread(
            NULL,
            0,
            ThreadProc,
            &data[i],
            0,
            NULL
        );

        if (threads[i] == NULL) {
            std::cerr << "Create thread error: " << GetLastError() << '\n';
            return;
        }
    }

    WaitForMultipleObjects(THREADS_NUM, threads, TRUE, INFINITE);

    for (int i = 1; i < THREADS_NUM; ++i) {
        size_t start = 0;
        size_t mid = i * partSize;
        size_t end = (i == THREADS_NUM - 1) ? FILE_SIZE : (i + 1) * partSize;

        Merge(buffer, start, mid, end);
    }


    for (HANDLE thread : threads)
        CloseHandle(thread);
}

void SyncIO(void (*SortFunction)(char*&, size_t, size_t)) {
    HANDLE hFile = CreateFileA(
        FILE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Open file error: " << GetLastError() << '\n';
        CloseHandle(hFile);
        return;
    }

    char* buffer = new char[FILE_SIZE];
    DWORD bytesRead;

    if (!ReadFile(hFile, buffer, FILE_SIZE, &bytesRead, NULL)) {
        std::cerr << "Read file error: " << GetLastError() << '\n';
        delete[] buffer;
        return;
    }

    SortFunction(buffer, 0, FILE_SIZE);

    DWORD bytesWrite;

    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);

    WriteFile(hFile, buffer, FILE_SIZE, &bytesWrite, NULL);

    CloseHandle(hFile);
}

void MappingIO(void (*SortFunction)(char*&, size_t, size_t)) {
    HANDLE hFile = CreateFileA(
        FILE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Open file error: " << GetLastError() << '\n';
        CloseHandle(hFile);
        return;
    }

    HANDLE hMapping = CreateFileMappingA(
        hFile,
        NULL,
        PAGE_READWRITE,
        0,
        0,
        NULL
    );

    if (hMapping == NULL) {
        std::cerr << "Create file mapping error: " << GetLastError() << '\n';
        CloseHandle(hFile);
        return;
    }

    char* mapView = (char*)MapViewOfFile(
        hMapping,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        0
    );

    if (mapView == NULL) {
        std::cerr << "Map view of file error: " << GetLastError() << '\n';
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return;
    }

    SortFunction(mapView, 0, FILE_SIZE);

    UnmapViewOfFile(mapView);

    CloseHandle(hMapping);
    CloseHandle(hFile);
}

void RunTests() {
    InitFile();
    auto start = std::chrono::high_resolution_clock::now();
    SyncIO(Sort);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> time = end - start;
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Sync time:\t\t" << time.count() << "\tseconds\n";

    InitFile();
    start = std::chrono::high_resolution_clock::now();
    MappingIO(Sort);
    end = std::chrono::high_resolution_clock::now();
    time = end - start;
    std::cout << "Mapping time:\t\t" << time.count() << "\tseconds\n";

    InitFile();
    start = std::chrono::high_resolution_clock::now();
    SyncIO(SortThreads);
    end = std::chrono::high_resolution_clock::now();
    time = end - start;
    std::cout << "Sync threads time:\t" << time.count() << "\tseconds\n";

    InitFile();
    start = std::chrono::high_resolution_clock::now();
    MappingIO(SortThreads);
    end = std::chrono::high_resolution_clock::now();
    time = end - start;
    std::cout << "Mapping threads time:\t" << time.count() << "\tseconds\n";
}

int main() {
    std::cout << "File size:\t" << FILE_SIZE << " bytes" << '\n';
    std::cout << "Threads number:\t" << THREADS_NUM << '\n';
    std::cout << "Tests number:\t" << TESTS_NUM << "\n\n";

    for (int i = 0; i < TESTS_NUM; ++i) {
        std::cout << "Test #" << i + 1 << "\n\n";
        RunTests();
        std::cout << "\n\n";
    }

    return 0;
}
