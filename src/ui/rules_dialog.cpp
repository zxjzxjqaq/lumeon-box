#include "ui/rules_dialog.hpp"
#include <algorithm>
#include <commctrl.h>
#include <windowsx.h>
#include "core/strutil.hpp"

#pragma comment(lib, "user32")

namespace lb {

namespace {
constexpr int ID_INPUT = 102, ID_COMBO = 103, ID_ADD = 100, ID_LIST = 107, ID_DEL = 101;
constexpr Category kCatOrder[] = {Category::Doc,    Category::Image,  Category::Video,
                                  Category::Audio,  Category::Archive, Category::Installer,
                                  Category::Other};
constexpr int kNeverItem = 7;  // 下拉最后一项 = 永不整理

std::wstring TrimLower(std::wstring s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'.')) s.erase(s.begin());
    while (!s.empty() && s.back() == L' ') s.pop_back();
    for (auto& ch : s) ch = static_cast<wchar_t>(towlower(ch));
    return s;
}
}  // namespace

void RulesDialog::Toggle(RuleSet& rules, const std::wstring& rulesFile) {
    if (hwnd_) {
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
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, 200, 200, 372, 300,
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
        HWND c = CreateWindowExW(
            WS_EX_CLIENTEDGE, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
        return c;
    };
    input_ = mk(L"EDIT", L"扩展名或文件名，如 apk / 周报.docx", ES_AUTOHSCROLL, 10, 10, 190, 26,
                ID_INPUT);
    combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                             206, 10, 96, 240, hwnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_COMBO)), inst,
                             nullptr);
    SendMessageW(combo_, WM_SETFONT, (WPARAM)font, TRUE);
    for (auto c : kCatOrder)
        ComboBox_AddString(combo_, CategoryName(c).c_str());
    ComboBox_AddString(combo_, L"永不整理");
    ComboBox_SetCurSel(combo_, 0);
    mk(L"BUTTON", L"添加", BS_PUSHBUTTON, 308, 10, 46, 26, ID_ADD);
    list_ = mk(L"LISTBOX", nullptr, LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL, 10, 44, 344,
               170, ID_LIST);
    mk(L"BUTTON", L"删除选中的规则", BS_PUSHBUTTON, 10, 220, 140, 26, ID_DEL);
    RebuildList();
}

void RulesDialog::RebuildList() {
    items_.clear();
    // 只展示自定义规则与白名单（默认规则隐式生效，不占视线）
    for (auto& e : rules_->Entries())
        if (e.custom) items_.push_back({false, e.ext});
    for (auto& k : rules_->NeverList()) items_.push_back({true, k});

    ListBox_ResetContent(list_);
    for (auto& [isNever, key] : items_) {
        std::wstring line;
        if (isNever) {
            line = key + L"  →  永不整理";
        } else {
            Category cat = rules_->Match(key);
            line = key + L"  →  " + CategoryName(cat);
        }
        ListBox_AddString(list_, line.c_str());
    }
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
            wchar_t buf[MAX_PATH]{};
            Edit_GetText(self->input_, buf, MAX_PATH);
            std::wstring key = TrimLower(buf);
            if (key.empty()) break;
            int sel = ComboBox_GetCurSel(self->combo_);
            if (sel == kNeverItem) {  // 永不整理
                if (key[0] != L'*' && key.find(L'.') == std::wstring::npos)
                    key = L"*." + key;  // 纯词 = 扩展名意图
                self->rules_->AddNever(key);
            } else {
                self->rules_->SetCustom(key, kCatOrder[sel < 0 ? 0 : sel]);
            }
            self->Save();
            Edit_SetText(self->input_, L"");
            self->RebuildList();
            break;
        }
        case ID_DEL: {
            int sel = ListBox_GetCurSel(self->list_);
            if (sel < 0 || sel >= (int)self->items_.size()) break;
            auto [isNever, key] = self->items_[sel];
            if (isNever)
                self->rules_->RemoveNever(key);
            else
                self->rules_->RemoveCustom(key);
            self->Save();
            self->RebuildList();
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
