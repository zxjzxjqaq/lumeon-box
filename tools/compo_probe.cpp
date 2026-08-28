// compo_probe —— 最小复现探针：只做 init_apartment + Compositor 创建
// 结果写 %TEMP%\compo_probe.log（供计划任务/WMI 等无控制台上下文读取）
#include <winrt/base.h>
#include <winrt/Windows.UI.Composition.h>
#include <cstdio>
#include <cstdlib>
#include <string>

static void Log(char const* s) {
    if (char const* tmp = std::getenv("TEMP"))
        if (FILE* f = std::fopen((std::string(tmp) + "\\compo_probe.log").c_str(), "a")) {
            std::fputs(s, f);
            std::fputc('\n', f);
            std::fclose(f);
        }
}

int main() {
    Log("step1: begin");
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        Log("step2: init_apartment ok");
    } catch (...) {
        Log("step2: init_apartment FAILED");
        return 2;
    }
    Log("step3: creating Compositor ...");
    try {
        winrt::Windows::UI::Composition::Compositor compositor;
        Log("step4: Compositor OK");
    } catch (winrt::hresult_error const& e) {
        Log("step4: Compositor hresult_error");
        char buf[128];
        std::snprintf(buf, sizeof(buf), "  hresult=0x%08X", static_cast<unsigned>(e.code()));
        Log(buf);
        return 3;
    } catch (...) {
        Log("step4: Compositor unknown exception");
        return 4;
    }
    return 0;
}
