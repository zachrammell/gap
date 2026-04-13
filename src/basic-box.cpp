#include "basic-box.h"

namespace UI::Widgets
{
    void basic_box(CmdBuffer::DrawList* lst, UIState* state, const BuildBoxInput& in, BuildBoxFlags flags)
    {
        // Process input.
        if (implies(flags, BuildBoxFlags::Clickable))
        {
            const auto& clip = CmdBuffer::current_clip(*lst);
            // Construct a clip for this box.
            auto box_clip = clip;
            box_clip.offset_x = offset_from(box_clip.offset_x, static_cast<int>(in.pos.x));
            box_clip.offset_y = offset_from(box_clip.offset_y, static_cast<int>(in.pos.y));
            box_clip.width = Width{ static_cast<int>(in.size.x) };
            box_clip.height = Height{ static_cast<int>(in.size.y) };

            // Clip the box if necessary.
            box_clip = intersect(clip, box_clip);
            if (mouse_in_clip(state->mouse.ui_mouse, box_clip))
            {
                try_set_hot_widget(state, in.id);
                if (down(*state, MouseButton::L))
                {
                    try_set_focus_widget(state, in.id);
                }
            }
        }

        const auto* palette = CmdBuffer::current_palette(*lst);

        if (implies(flags, BuildBoxFlags::Fill))
        {
            CmdBuffer::start_shapes(lst, Render::VertShader::OneOneTransform);
            CmdBuffer::solid_rect(lst, Render::FragShader::BasicColor, in.pos, in.size, palette->fill);
        }

        if (implies(flags, BuildBoxFlags::Strike))
        {
            CmdBuffer::start_shapes(lst, Render::VertShader::OneOneTransform);
            CmdBuffer::strike_rect(lst, Render::FragShader::BasicColor, in.pos, in.size, in.thickness, palette->border);
        }
    }
} // namespace UI::Widgets