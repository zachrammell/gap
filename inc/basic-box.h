#pragma once

#include "cmd-buffer-api.h"
#include "types.h"
#include "ui-common.h"

namespace UI::Widgets
{
    enum class BuildBoxFlags : uint32_t
    {
        None      = 0,
        Strike    = 1 << 0,
        Fill      = 1 << 1,
        Clickable = 1 << 2,
    };

    struct BuildBoxInput
    {
        ID id = ID::Zero;
        Vec2f pos{}; // Bottom-left.
        Vec2f size{};
        float thickness{};
    };

    void basic_box(CmdBuffer::DrawList* lst, UIState* state, const BuildBoxInput& in, BuildBoxFlags flags);
} // namespace UI::Widgets