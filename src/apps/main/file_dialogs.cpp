#include "file_dialogs.hpp"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>
#include <string>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

std::string wstr_to_utf8(const wchar_t* w) {
    if (!w) {
        return {};
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) {
        return {};
    }
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

} // namespace

bool pick_folder_dialog(std::string& out_path) {
    IFileOpenDialog* pfd = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
    if (FAILED(hr) || !pfd) {
        return false;
    }
    DWORD opts = 0;
    pfd->GetOptions(&opts);
    pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    hr = pfd->Show(nullptr);
    if (FAILED(hr)) {
        pfd->Release();
        return false;
    }
    IShellItem* psi = nullptr;
    hr = pfd->GetResult(&psi);
    pfd->Release();
    if (FAILED(hr) || !psi) {
        return false;
    }
    PWSTR path = nullptr;
    hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &path);
    psi->Release();
    if (FAILED(hr) || !path) {
        return false;
    }
    out_path = wstr_to_utf8(path);
    CoTaskMemFree(path);
    return true;
}

bool pick_open_yaml_file(std::string& out_path) {
    IFileOpenDialog* pfd = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
    if (FAILED(hr) || !pfd) {
        return false;
    }
    DWORD opts = 0;
    pfd->GetOptions(&opts);
    pfd->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
    static const COMDLG_FILTERSPEC spec[] = {
        {L"YAML", L"*.yml;*.yaml"},
        {L"All files", L"*.*"},
    };
    pfd->SetFileTypes(2, spec);
    pfd->SetFileTypeIndex(1);
    hr = pfd->Show(nullptr);
    if (FAILED(hr)) {
        pfd->Release();
        return false;
    }
    IShellItem* psi = nullptr;
    hr = pfd->GetResult(&psi);
    pfd->Release();
    if (FAILED(hr) || !psi) {
        return false;
    }
    PWSTR path = nullptr;
    hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &path);
    psi->Release();
    if (FAILED(hr) || !path) {
        return false;
    }
    out_path = wstr_to_utf8(path);
    CoTaskMemFree(path);
    return true;
}

bool pick_save_json_file(std::string& out_path) {
    IFileSaveDialog* psd = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&psd));
    if (FAILED(hr) || !psd) {
        return false;
    }
    DWORD opts = 0;
    psd->GetOptions(&opts);
    psd->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT);
    static const COMDLG_FILTERSPEC spec[] = {
        {L"JSON", L"*.json"},
    };
    psd->SetFileTypes(1, spec);
    psd->SetDefaultExtension(L"json");
    hr = psd->Show(nullptr);
    if (FAILED(hr)) {
        psd->Release();
        return false;
    }
    IShellItem* psi = nullptr;
    hr = psd->GetResult(&psi);
    psd->Release();
    if (FAILED(hr) || !psi) {
        return false;
    }
    PWSTR path = nullptr;
    hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &path);
    psi->Release();
    if (FAILED(hr) || !path) {
        return false;
    }
    out_path = wstr_to_utf8(path);
    CoTaskMemFree(path);
    return true;
}

#endif
