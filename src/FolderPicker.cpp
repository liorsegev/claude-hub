#include "FolderPicker.hpp"

#include <shobjidl.h>
#include <shlobj.h>

namespace ch {

namespace fs = std::filesystem;

namespace {

// Scoped COM release for raw interface pointers.
template <typename T>
struct ComPtr {
	T* p = nullptr;
	~ComPtr() { if (p) p->Release(); }
	T** addr() { return &p; }
	T* operator->() const { return p; }
	explicit operator bool() const { return p != nullptr; }
};

}

std::optional<fs::path>
pick_folder(HWND owner, const fs::path& initial) {
	// Scoped COM: don't disturb whatever threading model the rest of the app
	// picked. If COM was already initialized differently, CoInitializeEx
	// returns RPC_E_CHANGED_MODE and we carry on — IFileDialog is fine with
	// the existing apartment.
	const HRESULT init_hr = CoInitializeEx(nullptr,
		COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	const bool need_uninit = SUCCEEDED(init_hr);

	std::optional<fs::path> result;

	ComPtr<IFileOpenDialog> dialog;
	HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
		IID_IFileOpenDialog, reinterpret_cast<void**>(dialog.addr()));
	if (SUCCEEDED(hr) && dialog) {
		DWORD opts = 0;
		dialog->GetOptions(&opts);
		dialog->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);

		if (!initial.empty()) {
			ComPtr<IShellItem> item;
			if (SUCCEEDED(SHCreateItemFromParsingName(
					initial.wstring().c_str(), nullptr,
					IID_IShellItem, reinterpret_cast<void**>(item.addr()))) && item) {
				dialog->SetFolder(item.p);
			}
		}

		if (SUCCEEDED(dialog->Show(owner))) {
			ComPtr<IShellItem> chosen;
			if (SUCCEEDED(dialog->GetResult(chosen.addr())) && chosen) {
				PWSTR raw = nullptr;
				if (SUCCEEDED(chosen->GetDisplayName(SIGDN_FILESYSPATH, &raw)) && raw) {
					result = fs::path(raw);
					CoTaskMemFree(raw);
				}
			}
		}
	}

	if (need_uninit) CoUninitialize();
	return result;
}

}
