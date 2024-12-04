#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <shlwapi.h>
#include <iomanip>

#pragma comment(lib, "Shlwapi.lib")

HANDLE logFile;
HANDLE regFile;
std::string hKeyStr;
LONG changesCount = 0;
LONG failsCount = 0;

void WriteRegHeader() {
    const char* header = "Windows Registry Editor Version 5.00\n\n";
    DWORD written;
    WriteFile(regFile, header, strlen(header), &written, NULL);
}

std::string EscapeRegString(const std::string& str) {
    std::string result;
    for (char c : str) {
        if (c == '\\') {
            result += "\\\\";
        }
        else if (c == '\0') {
            break;
        }
        else {
            result += c;
        }
    }
    return result;
}

void SaveRegValue(const std::string& keyPath, const std::string& valueName, const std::string& valueData) {
    std::string output = "[" + hKeyStr + keyPath + "]\n";

    if (valueName.empty()) {
        output += "@=";
    }
    else {
        output += "\"" + valueName + "\"=";
    }

    output += "\"" + EscapeRegString(valueData) + "\"\n\n";

    DWORD written;
    WriteFile(regFile, output.c_str(), output.size(), &written, NULL);
}

void LogChange(const std::string& action, const std::string& keyPath, const std::string& valueName = "") {
    std::string logEntry;
    if (valueName.empty()) {
        logEntry = action + ": " + keyPath + '\n';
    }
    else {
        logEntry = action + ": " + keyPath + "\\" + valueName + '\n';
    }
    DWORD written;
    WriteFile(logFile, logEntry.c_str(), logEntry.size(), &written, NULL);
}

void DeleteRegistryValue(HKEY hKey, const std::string& valueName, const std::string& keyPath) {
    char valueData[16384];
    DWORD valueDataSize = sizeof(valueData);
    DWORD valueType;

    if (RegQueryValueExA(hKey, valueName.c_str(), NULL, &valueType,
        (LPBYTE)valueData, &valueDataSize) == ERROR_SUCCESS) {
        SaveRegValue(keyPath, valueName, std::string(valueData, valueDataSize));
    }

    if (RegDeleteValueA(hKey, valueName.c_str()) == ERROR_SUCCESS) {
        LogChange("Deleted value", hKeyStr + keyPath, valueName);
        changesCount++;
    }
    else {
        std::cerr << "Failed to delete value: " << valueName << '\n';
        failsCount++;
    }
}

bool FileExists(const std::string& filePath) {
    return PathFileExistsA(filePath.c_str());
}

bool IsValidFilePath(const std::string& path) {
    if (path.empty()) return false;

    if (path.length() < 3 || path[1] != ':' || path[2] != '\\') return false;

    for (char ch : path) {
        if (ch == ',' || ch == '/' || ch == '"' || ch == '^') return false;
    }

    size_t lastSlash = path.find_last_of("\\");
    if (lastSlash == std::string::npos || lastSlash == path.length() - 1) return false;

    std::string fileName = path.substr(lastSlash + 1);
    if (fileName.find('.') == std::string::npos) return false;

    return true;
}

bool IsKeyEmpty(HKEY hKey) {
    DWORD subKeyCount = 0, valueCount = 0;
    if (RegQueryInfoKeyA(hKey, NULL, NULL, NULL, &subKeyCount, NULL, NULL,
        &valueCount, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) {
        return false;
    }
    
    if (subKeyCount == 0 && valueCount == 0) {
        return true;
    }
    
    if (subKeyCount == 0 && valueCount == 1) {
        char valueName[16384];
        DWORD valueNameSize = sizeof(valueName);
        BYTE valueData[16384];
        DWORD valueDataSize = sizeof(valueData);
        DWORD valueType;

        if (RegEnumValueA(hKey, 0, valueName, &valueNameSize, NULL, &valueType,
            valueData, &valueDataSize) == ERROR_SUCCESS) {
            if (strlen(valueName) == 0 && valueType == REG_SZ &&
                (valueDataSize <= 1 || valueData[0] == '\0')) {
                return true;
            }
        }
    }

    return false;
}

void DeleteEmptyKey(HKEY hParentKey, const std::string& keyPath) {
    size_t lastBackslash = keyPath.find_last_of('\\');
    std::string parentPath = lastBackslash != std::string::npos ?
        keyPath.substr(0, lastBackslash) : "";
    std::string keyName = lastBackslash != std::string::npos ?
        keyPath.substr(lastBackslash + 1) : keyPath;

    if (RegDeleteKeyA(hParentKey, keyName.c_str()) == ERROR_SUCCESS) {
        LogChange("Deleted empty key", hKeyStr + keyPath);
        changesCount++;
    }
    else {
        std::cerr << "Failed to delete key" << keyPath << '\n';
        failsCount++;
    }
}

BOOL EnumRegistryKeys(HKEY hKey, const std::string& keyPath) {
    DWORD index = 0;
    char subKeyName[256];
    DWORD subKeyNameSize = sizeof(subKeyName);
    HKEY hSubKey;
    BOOL valueDeleted = false;

    while (RegEnumKeyExA(hKey, index, subKeyName, &subKeyNameSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
        std::string fullPath = keyPath + "\\" + subKeyName;

        if (RegOpenKeyExA(hKey, subKeyName, 0, KEY_READ | KEY_WRITE, &hSubKey) == ERROR_SUCCESS) {

            int isChanged = EnumRegistryKeys(hSubKey, fullPath);
            if (IsKeyEmpty(hSubKey) && isChanged) {
                RegCloseKey(hSubKey);
                std::cout << "Empty key " << hKeyStr + fullPath << "\n\n";
                DeleteEmptyKey(hKey, fullPath);
                index--;
            }
            else {
                RegCloseKey(hSubKey);
            }
        }

        subKeyNameSize = sizeof(subKeyName);
        index++;
    }

    DWORD valueIndex = 0;
    char valueName[16384];
    DWORD valueNameSize = sizeof(valueName);
    BYTE valueData[16384];
    DWORD valueDataSize = sizeof(valueData);
    DWORD valueType;

    while (RegEnumValueA(hKey, valueIndex, valueName, &valueNameSize, NULL, &valueType, valueData, &valueDataSize) == ERROR_SUCCESS) {
        std::string valueDataStr(reinterpret_cast<char*>(valueData), valueDataSize);
        std::string valueNameStr(reinterpret_cast<char*>(valueName), valueNameSize);
        if (valueType == REG_SZ && IsValidFilePath(valueDataStr) && !FileExists(valueDataStr)) {
            std::cout << "[" << hKeyStr << keyPath << "]\n";
            std::cout << (valueNameStr.empty() ? "@" : "\"" + valueNameStr + "\"") << "=\"" << valueDataStr << "\"\n\n";
            DeleteRegistryValue(hKey, valueName, keyPath);
            valueDeleted = true;
        }
        else {
            valueIndex++;
        }

        valueNameSize = sizeof(valueName);
        valueDataSize = sizeof(valueData);
    }

    return valueDeleted;
}

void CleanRegistry(HKEY hRootKey, const std::string& rootPath) {
    HKEY hKey;
    if (RegOpenKeyExA(hRootKey, rootPath.c_str(), 0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        EnumRegistryKeys(hKey, rootPath);
        RegCloseKey(hKey);
    }
    else {
        std::cerr << "Failed to open key: " << rootPath << '\n';
    }
}

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "ru");

    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <rootKey>\n";
        return 1;
    }

    std::cout << "Registry Cleaner\n\nCreating a log and backup file...\n";

    logFile = CreateFileA("registry_cleanup_log.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    regFile = CreateFileA("registry_backup.reg", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (logFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to create log file.\n";
        return 1;
    }
    if (regFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to create reg file.\n";
        return 1;
    }

    std::cout << "Creation successful\n\nStart...\n\n";

    WriteRegHeader();

    HKEY hRootKey;
    if (std::string(argv[1]) == "HKEY_LOCAL_MACHINE") {
        hRootKey = HKEY_LOCAL_MACHINE;
    }
    else if (std::string(argv[1]) == "HKEY_CURRENT_USER") {
        hRootKey = HKEY_CURRENT_USER;
    }
    else if (std::string(argv[1]) == "HKEY_CLASSES_ROOT") {
        hRootKey = HKEY_CLASSES_ROOT;
    }
    else if (std::string(argv[1]) == "HKEY_USERS") {
        hRootKey = HKEY_USERS;
    }
    else if (std::string(argv[1]) == "HKEY_CURRENT_CONFIG") {
        hRootKey = HKEY_CURRENT_CONFIG;
    }
    else {
        std::cerr << "Invalid root key.\n";
        return 1;
    }

    hKeyStr = argv[1];

    std::string rootPath = "";

    CleanRegistry(hRootKey, rootPath);

    CloseHandle(logFile);
    CloseHandle(regFile);

    std::cout << "Cleanup completed.\nChanges: " << changesCount
        << "\nFails: " << failsCount
        << "\nCheck registry_cleanup_log.txt for details and registry_backup.txt for rollback.\n";
    return 0;
}
