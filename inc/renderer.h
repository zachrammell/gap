#pragma once

#include <memory>
#include <string_view>

#include "os.h"
#include "types.h"
#include "vec.h"

namespace Feed
{
    class MessageFeed;
} // namespace Feed

namespace CmdBuffer
{
    struct CmdList;
} // namespace CmdBuffer

namespace Render
{
    Camera cursor_camera_transform(const Camera& camera,
                                    Vec2f target,
                                    float target_scale_x,
                                    float zoom_factor_x,
                                    float delta_time);

    WorldCamera cursor_camera_transform(const WorldCamera& camera,
                                    Vec2d target,
                                    double target_scale_x,
                                    double zoom_factor_x,
                                    float delta_time);

    Vec2f screen_to_world_transform(const Camera& camera,
                                    Vec2f point,
                                    const ScreenDimensions& screen);

    enum class ViewportOffsetX : int { };
    enum class ViewportOffsetY : int { };

    struct RenderViewport
    {
        ViewportOffsetX offset_x;
        ViewportOffsetY offset_y;
        Width width;
        Height height;

        static RenderViewport basic(const ScreenDimensions& screen);

        bool operator==(const RenderViewport&) const = default;
    };

    struct FrameRenderer;

    enum class GlyphOffsetX : int { };
    enum class GlyphOffsetY : int { };

    struct GlyphEntry
    {
        GlyphOffsetX offset_x;
        GlyphOffsetY offset_y;
        Width width;
        Height height;
        const uint8_t* buffer;
    };

    enum class BasicTextureOffsetX : int { };
    enum class BasicTextureOffsetY : int { };

    struct BasicTextureEntry
    {
        BasicTextureOffsetX offset_x;
        BasicTextureOffsetY offset_y;
        Width width;
        Height height;
        const uint8_t* buffer;
    };

    struct FrameRenderer;

    // OS interaction.
    bool os_init_renderer_window(OS::OSWindow wind);
    void os_select_renderer(OS::OSWindow wind);
    void os_destroy_renderer_window(OS::OSWindow wind);
    void os_swap_buffers(OS::OSWindow wind);

    // Creation.
    FrameRenderer* make_platform_renderer(Arena::Arena* arena);

    // Interaction.
    void update_resolution(FrameRenderer* renderer, Vec2f new_res);
    void update_time(FrameRenderer* renderer, float app_time, float dt);

    // Initialize global data for all renderer instances.
    bool init(const ScreenDimensions& screen);
    // Reloads all shaders for every renderer instance.
    void reload_shaders(Feed::MessageFeed* feed);

    // Helper functions.
    void draw_cmd_list(FrameRenderer* renderer, CmdBuffer::CmdList* lst);
    void apply_framebuffer(Render::FrameRenderer* renderer, const ScreenDimensions& screen);
    void window_end_frame(OS::OSWindow window);

    // Resize-related functionality.
    void screen_resize(const ScreenDimensions& screen);

    // Functions for creating basic textures and manipulating them.
    BasicTexture create_basic_texture(const ScreenDimensions& size);
    void delete_basic_texture(BasicTexture tex);
    void submit_basic_texture_data(BasicTexture tex, BasicTextureEntry entry);

    // Functions for creating glyph cache textures, binding, and manipulating them.
    BasicTexture create_glyph_texture(const ScreenDimensions& dim);
    void submit_glyph_data(BasicTexture tex, GlyphEntry entry);

    // APIs for general frame requests.
    void request_frames();
    int frames_remaining();
    void consume_frame();

    // APIs for initialization/debugging.
    bool init_renderer_arenas();
    void display_renderer_version();
} // namespace Render