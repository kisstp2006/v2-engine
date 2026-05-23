// STL ELŐSZÖR.
#include <string>

#include <windows.h>
#include <shobjidl.h>

#pragma comment(lib, "ole32.lib")

#include "file_picker.h"

namespace editor {

namespace {

std::string utf16to8(const wchar_t* w) {
    if (!w || !*w) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out((size_t)(n - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring utf8to16(const char* s) {
    if (!s || !*s) return {};
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring out((size_t)(n - 1), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), n);
    return out;
}

}  // namespace

std::string pickFile(const char* title, const char* extension, bool save) {
    std::string result;

    HRESULT hr = ::CoInitializeEx(
        nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool needUninit = SUCCEEDED(hr);

    IFileDialog* dlg = nullptr;
    REFCLSID    clsid = save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog;
    REFIID      iid   = save ? IID_IFileSaveDialog : IID_IFileOpenDialog;
    hr = ::CoCreateInstance(clsid, nullptr, CLSCTX_ALL, iid,
                            reinterpret_cast<void**>(&dlg));
    if (SUCCEEDED(hr) && dlg) {
        if (title && *title) {
            std::wstring wt = utf8to16(title);
            dlg->SetTitle(wt.c_str());
        }
        if (extension && *extension) {
            std::wstring wext = utf8to16(extension);
            dlg->SetDefaultExtension(wext.c_str());
            std::wstring wfilt = L"*." + wext;
            COMDLG_FILTERSPEC spec[1] = { { L"Scene file", wfilt.c_str() } };
            dlg->SetFileTypes(1, spec);
        }

        hr = dlg->Show(nullptr);
        if (SUCCEEDED(hr)) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                PWSTR raw = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw))
                    && raw) {
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
