#include "ui/rules_dialog.hpp"
#include <commctrl.h>
#include <windowsx.h>
#include "core/strutil.hpp"

#pragma comment(lib, "user32")

namespace lb {

namespace {
constexpr int ID_LIST = 107, ID_EXT = 102, ID_COMBO = 103, ID_ADD = 100, ID_DEL = 101,
              ID_NLIST = 108, ID_NEDIT = 104, ID_NADD = 105, ID_NDEL = 106;
constexpr Category kCatOrder[] = {Category::Doc,    Category::Image,  Category::Video,
                                  Category::Audio,  Category::Archive, Category::Installer,
                                  Category::Other};

std::wstring TrimLower(std::wstring s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'.')) s.erase(s.begin());
    while (!s.empty() && s.back() == L' ') s.pop_back();
    for (auto& ch : s) ch = static_cast<wchar_t>(towlower(ch));
    return s;
}
}  // namespace

void RulesDialog::Toggle(RuleSet& rules, const std::wstring& rulesFile) {
    if (hwnd_) {  // 已开 → 聚焦
        SetForegroundWindow(hwnd_);
        return;
    }
    rules_ = &rules;
    file_ = rulesFile;
    WNDCLASSW wc{};
    wc.lpfnWndProc = &WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(18, 20, 28));
    wc.lpszClassName = L"LumenBoxRules";
    RegisterClassW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"LumenBox 分类规则",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, 200, 160, 380, 600,
                            nullptr, nullptr, wc.hInstance, this);
    ShowWindow(hwnd_, SW_SHOW);
}

void RulesDialog::BuildControls(HWND hwnd) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    HFONT font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w,
                  int h, int id) {
        HWND c = CreateWindowExW(WS_EX_CLIENTEDGE, cls, text, WS_CHILD | WS_VISIBLE | style, x,
                                 y, w, h, hwnd, reinterpret_cast<HMENU>(
                                                     static_cast<INT_PTR>(id)),
                                 inst, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
        return c;
    };
    list_ = mk(L"LISTBOX", nullptr, LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL, 10, 10, 344,
               200, ID_LIST);
    mk(L"STATIC", L"扩展名 → 格子（自定义覆盖默认）", 0, 10, 218, 300, 20, -1);
    extEdit_ = mk(L"EDIT", L"", ES_AUTOHSCROLL, 10, 242, 100, 24, ID_EXT);
    catCombo_ = mk(L"COMBOBOX", nullptr, CBS_DROPDOWNLIST | CBS_HASSTRINGS, 118, 242, 140, 200,
                   ID_COMBO);
    for (auto c : kCatOrder)
        SendMessageW(catCombo_, CB_ADDSTRING, 0, (LPARAM)CategoryName(c).c_str());
    SendMessageW(catCombo_, CB_SETCURSEL, 0, 0);
    mk(L"BUTTON", L"添加/覆盖", BS_PUSHBUTTON, 266, 242, 88, 26, ID_ADD);
    mk(L"BUTTON", L"删除自定义", BS_PUSHBUTTON, 266, 274, 88, 26, ID_DEL);
    mk(L"STATIC", L"永不整理（文件名 或 *.扩展名）", 0, 10, 312, 320, 20, -1);
    neverEdit_ = mk(L"EDIT", L"", ES_AUTOHSCROLL, 10, 336, 200, 24, ID_NEDIT);
    mk(L"BUTTON", L"加入白名单", BS_PUSHBUTTON, 218, 336, 136, 26, ID_NADD);
    neverList_ = mk(L"LISTBOX", nullptr, LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL, 10,
                    366, 344, 150, ID_NLIST);
    mk(L"BUTTON", L"移除白名单", BS_PUSHBUTTON, 10, 522, 136, 26, ID_NDEL);
    RebuildLists();
}

void RulesDialog::RebuildLists() {
    ListBox_ResetContent(list_);
    for (auto& e : rules_->Entries()) {
        std::wstring line = L"." + e.ext + L"  →  " + CategoryName(e.cat) +
                            (e.custom ? L"    [自定义]" : L"    [默认]");
        ListBox_AddString(list_, line.c_str());
    }
    ListBox_ResetContent(neverList_);
    for (auto& k : rules_->NeverList()) ListBox_AddString(neverList_, k.c_str());
}

void RulesDialog::Save() {
    rules_->Save(file_);
}

LRESULT CALLBACK RulesDialog::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<RulesDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(
                              reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_CREATE:
        self->BuildControls(hwnd);
        return 0;
    case WM_COMMAND: {
        switch (LOWORD(wp)) {
        case ID_ADD: {
            wchar_t buf[128]{};
            Edit_GetText(self->extEdit_, buf, 128);
            std::wstring ext = TrimLower(buf);
            if (ext.empty()) break;
            int sel = ComboBox_GetCurSel(self->catCombo_);
            Category cat = kCatOrder[sel < 0 ? 0 : sel];
            self->rules_->SetCustom(ext, cat);
            self->Save();
            self->RebuildLists();
            break;
        }
        case ID_DEL: {
            int sel = ListBox_GetCurSel(self->list_);
            if (sel < 0) break;
            auto entries = self->rules_->Entries();
            if (sel < (int)entries.size() && entries[sel].custom) {
                self->rules_->RemoveCustom(entries[sel].ext);
                self->Save();
                self->RebuildLists();
            }
            break;
        }
        case ID_NADD: {
            wchar_t buf[MAX_PATH]{};
            Edit_GetText(self->neverEdit_, buf, MAX_PATH);
            std::wstring key = TrimLower(buf);
            if (key.empty()) break;
            // 用户只输扩展名时自动补 *. 前缀
            if (key[0] != L'*' && key.find(L'.') == std::wstring::npos) key = L"*." + key;
            self->rules_->AddNever(key);
            self->Save();
            Edit_SetText(self->neverEdit_, L"");
            self->RebuildLists();
            break;
        }
        case ID_NDEL: {
            int sel = ListBox_GetCurSel(self->neverList_);
            if (sel < 0) break;
            auto it = self->rules_->NeverList().begin();
            std::advance(it, sel);
            self->rules_->RemoveNever(*it);
            self->Save();
            self->RebuildLists();
            break;
        }
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        self->hwnd_ = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace lb
