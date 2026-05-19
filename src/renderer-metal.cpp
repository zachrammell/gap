
namespace Render
{
  struct FrameRenderer { };
  FrameRenderer* make_platform_renderer(Arena::Arena* arena)
  {
    return Arena::push_array<FrameRenderer>(arena, 1);
  }

  void update_resolution(FrameRenderer*, Vec2f new_res)
  {
    //- brt: NYI
  }

  void update_time(FrameRenderer*, float app_time, float /*dt*/)
  {
    //- brt: NYI
  }

  bool init(const ScreenDimensions& screen)
  {
    //- brt: NYI
    bool good = true;
    return good;
  }

  BasicTexture create_basic_texture(const ScreenDimensions& size)
  {
    //- brt: NYI
    return BasicTexture{};
  }

  void delete_basic_texture(BasicTexture tex)
  {
    //- brt: NYI
  }

  void submit_basic_texture_data(BasicTexture tex, BasicTextureEntry entry)
  {
    //- brt: NYI
  }

  BasicTexture create_glyph_texture(const ScreenDimensions& dim)
  {
    //- brt: NYI
    return BasicTexture{};
  }

  void submit_glyph_data(BasicTexture tex, GlyphEntry entry)
  {
    //- brt: NYI
  }

  void request_frames()
  {
    //- brt: NYI
  }

  int frames_remaining()
  {
    //- brt: NYI
    return 4;
  }

  void consume_frame()
  {
    //- brt: NYI
  }

  void draw_cmd_list(FrameRenderer*, CmdBuffer::CmdList* cmd_lst)
  {
    //- brt: NYI
  }

  void apply_framebuffer(Render::FrameRenderer*, const ScreenDimensions& screen)
  {
    //- brt: NYI
  }

  void window_end_frame(OS::OSWindow window)
  {
    //- brt: NYI
  }

  void screen_resize(const ScreenDimensions& screen)
  {
    //- brt: NYI
  }

  bool init_renderer_arenas()
  {
    //- brt: NYI
    return true;
  }

  void display_renderer_version()
  {
    printf("Metal\n");
  }
}
