#pragma once
#include <string>

namespace lb {

// 图片 OCR（Windows.Media.Ocr，纯本地不联网）。输入 PNG 字节，输出识别文字
// （按行拼接，\n 分隔）；引擎不可用/识别失败/无文字 → 空串。后台线程调用。
// 调用线程须已初始化 COM/WinRT _apartment（ClipboardMonitor worker 满足）。
std::wstring OcrPngBytes(const std::string& png);

}  // namespace lb