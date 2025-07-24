#include "Core/Log.h"

#include "Platform/Windows/WindowsDialogWindow.h"
#include <Windows.h>
#include <commdlg.h>
#include <knownfolders.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>


#pragma comment(lib, "shlwapi.lib")

namespace NeonEngine
{

std::optional<std::string> WindowsDialogWindow::showDialog(
    DialogType                                              type,
    const std::string                                      &title,
    const std::vector<std::pair<std::string, std::string>> &filters)
{

    // Use modern COM file dialog for Windows
    if (type == DialogType::OpenFile) {
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (SUCCEEDED(hr)) {
            IFileOpenDialog *pFileOpen = nullptr;
            hr                         = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_PPV_ARGS(&pFileOpen));

            if (SUCCEEDED(hr)) {
                // Set dialog title
                pFileOpen->SetTitle(std::wstring(title.begin(), title.end()).c_str());

                // Set file filters if specified
                if (!filters.empty()) {
                    std::vector<COMDLG_FILTERSPEC> fileTypes;
                    std::vector<std::wstring>      filterSpecs;
                    std::vector<std::wstring>      filterNames;

                    filterSpecs.reserve(filters.size());
                    filterNames.reserve(filters.size());

                    for (const auto &filter : filters) {
                        filterNames.push_back(std::wstring(filter.first.begin(), filter.first.end()));
                        filterSpecs.push_back(std::wstring(filter.second.begin(), filter.second.end()));
                    }

                    for (size_t i = 0; i < filters.size(); i++) {
                        COMDLG_FILTERSPEC spec;
                        spec.pszName = filterNames[i].c_str();
                        spec.pszSpec = filterSpecs[i].c_str();
                        fileTypes.push_back(spec);
                    }

                    pFileOpen->SetFileTypes(static_cast<UINT>(fileTypes.size()), fileTypes.data());
                }

                // Show dialog
                hr = pFileOpen->Show(NULL);

                if (SUCCEEDED(hr)) {
                    IShellItem *pItem = nullptr;
                    hr                = pFileOpen->GetResult(&pItem);

                    if (SUCCEEDED(hr)) {
                        PWSTR pszFilePath;
                        hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);

                        if (SUCCEEDED(hr)) {
                            // Convert wide string to regular string
                            int         size_needed = WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, NULL, 0, NULL, NULL);
                            std::string result(size_needed, 0);
                            WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, &result[0], size_needed, NULL, NULL);
                            if (size_needed > 0) {
                                result.resize(result.size() - 1); // Remove null terminator
                            }

                            CoTaskMemFree(pszFilePath);
                            pItem->Release();
                            pFileOpen->Release();
                            CoUninitialize();

                            return result;
                        }
                        pItem->Release();
                    }
                }
                pFileOpen->Release();
            }
            CoUninitialize();
        }
    }
    // For folder selection
    else if (type == DialogType::SelectFolder) {
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (SUCCEEDED(hr)) {
            IFileOpenDialog *pFileOpen = nullptr;
            hr                         = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_PPV_ARGS(&pFileOpen));

            if (SUCCEEDED(hr)) {
                DWORD dwOptions;
                pFileOpen->GetOptions(&dwOptions);
                pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS);

                // Set dialog title
                pFileOpen->SetTitle(std::wstring(title.begin(), title.end()).c_str());

                // Show dialog
                hr = pFileOpen->Show(NULL);

                if (SUCCEEDED(hr)) {
                    IShellItem *pItem = nullptr;
                    hr                = pFileOpen->GetResult(&pItem);

                    if (SUCCEEDED(hr)) {
                        PWSTR pszFolderPath;
                        hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFolderPath);

                        if (SUCCEEDED(hr)) {
                            // Convert wide string to regular string
                            int         size_needed = WideCharToMultiByte(CP_UTF8, 0, pszFolderPath, -1, NULL, 0, NULL, NULL);
                            std::string result(size_needed, 0);
                            WideCharToMultiByte(CP_UTF8, 0, pszFolderPath, -1, &result[0], size_needed, NULL, NULL);
                            result.resize(result.size() - 1); // Remove null terminator

                            CoTaskMemFree(pszFolderPath);
                            pItem->Release();
                            pFileOpen->Release();
                            CoUninitialize();

                            return result;
                        }
                        pItem->Release();
                    }
                }
                pFileOpen->Release();
            }
            CoUninitialize();
        }
    }

    // Return empty optional if dialog was cancelled or failed
    return std::nullopt;
}

} // namespace NeonEngine
