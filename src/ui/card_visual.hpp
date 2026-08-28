#pragma once
#include "ui/d2d_interop.hpp"

namespace lb {

// 用 D2D 在 DComp 表面上绘制 Aurora Glass 卡片，包成视觉节点挂到 root
bool CreateCardVisual(GraphicsContext& gfx, ComPtr<IDCompositionVisual>& out,
                      float wDip, float hDip, float dpiScale);

}  // namespace lb
