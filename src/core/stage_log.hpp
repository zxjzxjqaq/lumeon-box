#pragma once
// 崩溃定位：阶段标记追加到 %TEMP%\lumenbox-stage.log（M0 临时手段）
#include <cstdio>
#include <cstdlib>
#include <string>

namespace lb {

inline void StageTrace(char const* stage) {
    if (char const* tmp = std::getenv("TEMP"))
        if (std::FILE* f = std::fopen((std::string(tmp) + "\\lumenbox-stage.log").c_str(), "a")) {
            std::fputs(stage, f);
            std::fputc('\n', f);
            std::fclose(f);
        }
}

}  // namespace lb
