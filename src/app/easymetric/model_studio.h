// Aseprite 2.0
// Copyright (C) 2026 Aseprite 2.0 contributors
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_EASYMETRIC_MODEL_STUDIO_H_INCLUDED
#define APP_EASYMETRIC_MODEL_STUDIO_H_INCLUDED
#pragma once

#include <functional>

namespace ui {
class Widget;
}
namespace app {
class DocView;
}

namespace app::easymetric {

ui::Widget* createModelStudio(DocView& view,
                              std::function<void()> surfaces,
                              std::function<void()> close);

} // namespace app::easymetric
#endif
