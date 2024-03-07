#include <iostream>
#include <Windows.h>
#include <vector>
#include <sstream>
#include <conio.h>
#include <Shlobj.h>
#include <cstring>

static const std::wstring STARTUP_KEYS_PATH = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

class StartupItem {
public:
    virtual std::wstring GetDisplayName() = 0;
    virtual std::wstring GetStartupPath() = 0;
    virtual bool Delete() = 0;
};

class RegistryStartupItem: public StartupItem {
private:
    HKEY hRun = NULL;
    std::wstring displayName;
    std::wstring startupPath;
public:
    RegistryStartupItem(HKEY hRun, WCHAR name[MAX_PATH], WCHAR* value) {
        this->hRun = hRun;
        this->displayName = std::wstring(name);
        this->startupPath = std::wstring(value);
    }

    std::wstring GetDisplayName()
    {
        return this->displayName;
    }

    std::wstring GetStartupPath() 
    {
        return this->startupPath;
    }

    bool Delete() 
    {
        LONG result = RegDeleteValue(hRun, this->displayName.c_str());
        return result == ERROR_SUCCESS;
    }
};

class ShellStartupItem : public StartupItem {
private:
    std::wstring fullPath;
    std::wstring displayName;

public:
    ShellStartupItem(std::wstring base, WCHAR* fileName): displayName(fileName) {
        this->fullPath = base + L"\\" + displayName; 
    }

    std::wstring GetDisplayName() {
        return displayName;
    }

    std::wstring GetStartupPath() {
        return fullPath;
    }

    bool Delete() {
        return DeleteFile(fullPath.c_str()) != 0;
    }
};

static bool ScanForRegistryEntries(std::vector<std::shared_ptr<StartupItem>>& startupItems, HKEY& hRun) {
    LONG result = RegOpenKeyEx(HKEY_CURRENT_USER, STARTUP_KEYS_PATH.c_str(), 0,
        KEY_READ | KEY_WRITE, &hRun);
    DWORD index = 0;
    DWORD valueKind;
    WCHAR valueName[MAX_PATH] = { 0 };
    BYTE valueData[MAX_PATH] = { 0 };
    DWORD valueDataSize = sizeof(valueData);
    DWORD valueNameSize = MAX_PATH;

    if (result != ERROR_SUCCESS) {
        std::cerr << "RegOpenKeyEx: Error code: " << result << std::endl;
        return false;
    }

    for (;; index++, valueNameSize = MAX_PATH,
                     valueDataSize = sizeof(valueData))
    {
        result = RegEnumValue(hRun, index, valueName, &valueNameSize, NULL, &valueKind, valueData, &valueDataSize);
        switch (result) {
        case ERROR_SUCCESS:
            break;
        case ERROR_NO_MORE_ITEMS:
            return true;
        default:
            std::cerr << "RegEnumValue: Error code: " << result << std::endl;
            return false;
        }

        // we only want string values
        if (valueKind != REG_SZ && valueKind != REG_EXPAND_SZ) 
            continue;

        startupItems.push_back(std::make_shared<RegistryStartupItem>(hRun, valueName, reinterpret_cast<WCHAR*>(valueData)));
    }

    return true;
}

static bool ScanShellStartup(std::vector<std::shared_ptr<StartupItem>>& startupItems)
{
    PWSTR startupPath = nullptr;
    HRESULT result = SHGetKnownFolderPath(FOLDERID_Startup, 0, NULL, &startupPath);
    WIN32_FIND_DATA hFileData;

    if (!SUCCEEDED(result))
        return false;

    std::wstring scanPath(startupPath);
    CoTaskMemFree(startupPath);

    HANDLE hEntry = FindFirstFile((scanPath + L"\\*").c_str(), &hFileData);

    if (hEntry == INVALID_HANDLE_VALUE)
        return false;

    do {
        if (!(hFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && std::wcscmp(hFileData.cFileName, L"desktop.ini"))
            startupItems.push_back(std::make_shared<ShellStartupItem>(scanPath, hFileData.cFileName));

    } while (FindNextFile(hEntry, &hFileData));

    FindClose(hEntry);

    return true;
}

static std::vector<std::string> SplitString(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::istringstream iss(s);
    std::string token;

    while (std::getline(iss, token, delimiter)) {
        tokens.push_back(token);
    }

    return tokens;
}

int main(void)
{
    setlocale(LC_ALL, ".utf8");

    std::wcout << L"Zlophyn - The Small Windows® Startup Editor That Doesn't Sucks.\n" 
                  L"By Ryster Diffusion (2024)\n";

    auto items = std::vector<std::shared_ptr<StartupItem>>();
    HKEY hRun = NULL;

    if (!ScanForRegistryEntries(items, hRun)) {
        std::cerr << "Error! Unable to scan for registry entries.\n";
        return 1;
    }

    if (!ScanShellStartup(items)) {
        std::cerr << "Error! Unable to scan shell:startup for entries.\n";
        return 1;
    }

    std::cout << "Entries: " << "\n";
    
    for (std::size_t index = 0; index < items.size(); ++index) {
        auto& item = items[index];
        std::wcout << "["  << index << "]" << " " << item->GetDisplayName() << ": " << item->GetStartupPath() << "\n";
    }

    std::cout << "Then, what entries do you want to delete (Separated by ,): ";
    
    std::string whatToRemove;
    std::cin >> whatToRemove;
    auto tokens = SplitString(whatToRemove, ',');
    
    for (auto& token : tokens) 
        try {
            int index = std::stoi(token);
            auto& item = items.at(index);
            std::cout << "Removing " << index << "..." << "\n";

            if (!item->Delete()) 
                std::cout << "Unable to delete value!\n";
        }
        catch (std::exception) {
            std::cerr << "Ignoring " << token << " because it's invalid.\n";
        }


    if (hRun)
        RegCloseKey(hRun);

    std::cout << "Press any key to exit... ";
    std::cout.flush();
    (void) _getch();

    return 0;
}

