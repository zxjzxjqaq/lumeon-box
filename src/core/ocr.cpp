#include "core/ocr.hpp"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include <mutex>

namespace lb {
namespace wf = winrt::Windows::Foundation;
namespace wgi = winrt::Windows::Graphics::Imaging;
namespace wmo = winrt::Windows::Media::Ocr;
namespace wss = winrt::Windows::Storage::Streams;

std::wstring OcrPngBytes(const std::string& png) {
    if (png.empty()) return L"";
    try {
        static std::once_flag once;
        static wmo::OcrEngine engine{nullptr};
        std::call_once(once, [] {
            engine = wmo::OcrEngine::TryCreateFromUserProfileLanguages();
            if (!engine) {
                for (const wchar_t* tag : {L"zh-Hans", L"zh-CN", L"en-US"}) {
                    try {
                        winrt::Windows::Globalization::Language lang(tag);
                        engine = wmo::OcrEngine::TryCreateFromLanguage(lang);
                        if (engine) break;
                    } catch (...) {
                    }
                }
            }
        });
        if (!engine) return L"";
        wss::InMemoryRandomAccessStream stream;
        wss::DataWriter writer(stream);
        writer.WriteBytes(winrt::array_view<const uint8_t>(
            reinterpret_cast<const uint8_t*>(png.data()), (uint32_t)png.size()));
        writer.StoreAsync().get();
        writer.DetachStream();
        stream.Seek(0);
        auto decoder = wgi::BitmapDecoder::CreateAsync(stream).get();
        auto bitmap = decoder.GetSoftwareBitmapAsync().get();
        auto result = engine.RecognizeAsync(bitmap).get();
        std::wstring out;
        for (auto&& line : result.Lines()) {
            if (!out.empty()) out += L'\n';
            out += line.Text().c_str();
        }
        return out;
    } catch (...) {
        return L"";  // 引擎缺失/解码失败：静默降级，不影响剪贴板主流程
    }
}

}  // namespace lb