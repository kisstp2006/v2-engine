// STL FIRST.
#include <string>

// Windows-specific folder picker (IFileOpenDialog).
#include <windows.h>
#include <shobjidl.h>

#pragma comment(lib, "ole32.lib")

#include "folder_picker.h"

namespace editor {

namespace {

std::string utf16to8(const wchar_t* w) {
    if (!w || !*w) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(static_cast<size_t>(n - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring utf8to16(const char* s) {
    if (!s || !*s) return {};
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring out(static_cast<size_t>(n - 1), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), n);
    return out;
}

}  // namespace

std::string pickFolder(const char* title) {
    std::string result;

    HRESULT hr = ::CoInitializeEx(
        nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool needUninit = SUCCEEDED(hr);

    IFileOpenDialog* dlg = nullptr;
    hr = ::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                            IID_IFileOpenDialog,
                            reinterpret_cast<void**>(&dlg));
    if (SUCCEEDED(hr) && dlg) {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST |
                        FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);

        if (title && *title) {
            std::wstring wt = utf8to16(title);
            dlg->SetTitle(wt.c_str());
        }

        hr = dlg->Show(nullptr);
        if (SUCCEEDED(hr)) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                PWSTR raw = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) &&
                    raw) {
                    result = utf16to8(raw);
                    ::CoTaskMemFree(raw);
                }
                item->Release();
            }
        }
        dlg->Release();
    }

    if (needUninit) ::CoUninitialize();
    return result;
}

}  // namespace editor
