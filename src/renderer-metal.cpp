#include "renderer.h"

#include "assets.h"
#include "cmd-buffer.h"
#include "config.h"
#include "constants.h"
#include "feed.h"
#include "util.h"

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

namespace Render
{
  namespace
  {
    struct MetalWindowData
    {
      CAMetalLayer *layer;
      id<MTLTexture> scratch_color;
      id<MTLTexture> stage_color;
      id<CAMetalDrawable> drawable;
      id<MTLCommandBuffer> cmd_buffer;
    };

    struct MetalEntity
    {
      MetalEntity *next;
      MetalEntity *prev;
      union
      {
        id<MTLTexture> texture;
        struct
        {
          id<MTLBuffer> vertex_buffer;
          id<MTLBuffer> index_buffer;
        };
      };
      uint64_t gen;
    };

    struct MetalEntityList
    {
      MetalEntity *first;
      MetalEntity *last;
      uint64_t count;
    };

    struct MetalRenderData
    {
      Arena::Arena *render_arena;
      ScreenDimensions screen_size;
      MetalEntity *entity_free_lst;
      MetalEntityList tex_list;
      MetalEntityList flush_buffer_list;
      uint64_t entity_gen;
      id<MTLDevice> mtl_device;
      id<MTLCommandQueue> mtl_command_queue;
      int requested_frames;
    };

    MetalWindowData impl_metal_wnd_data;
    MetalRenderData impl_metal_rend_data;

    MetalWindowData *metal_wind_backend_data()
    {
      return &impl_metal_wnd_data;
    }

    MetalRenderData *metal_render_backend_data()
    {
      return &impl_metal_rend_data;
    }

    MetalEntity *push_render_entity(MetalRenderData *data, MetalEntityList *lst)
    {
      MetalEntity *node = data->entity_free_lst;
      if (node != nullptr)
      {
        SLLStackPop(data->entity_free_lst);
        zero_bytes(node);
      }
      else
      {
        node = Arena::push_array<MetalEntity>(data->render_arena, 1);
      }
      node->gen = data->entity_gen++;
      DLLPushBack(lst->first, lst->last, node);
      ++lst->count;
      return node;
    }

    void release_render_entity(MetalRenderData *data, MetalEntityList *lst, MetalEntity *e)
    {
      DLLRemove(lst->first, lst->last, e);
      --lst->count;
      SLLStackPush(data->entity_free_lst, e);
    }

    BasicTexture to_basic_texture(MetalEntity *e)
    {
      return BasicTexture{ reinterpret_cast<PrimitiveType<BasicTexture>>(e) };
    }

    MetalEntity *basic_texture_metal(BasicTexture tex)
    {
      return reinterpret_cast<MetalEntity *>(tex);
    }

    // brt: FIXME: reorder this crap
    NSString *ns_string_from_str8(String8 string)
    {
      NSString *result = [[NSString alloc] initWithBytes:string.str
                                                  length:string.size
                                                encoding:NSUTF8StringEncoding];
      return result;
    }
  }

  struct FrameRenderer { };

  bool os_init_renderer_window(OS::OSWindow wind)
  {
    MetalWindowData *wnd_data = metal_wind_backend_data();
    MetalRenderData *rend_data = metal_render_backend_data();
    NSWindow *ns_win = reinterpret_cast<NSWindow *>(wind);
    wnd_data->layer = [CAMetalLayer layer];
    wnd_data->layer.autoresizingMask = kCALayerHeightSizable | kCALayerWidthSizable;
    wnd_data->layer.needsDisplayOnBoundsChange = YES;
    // brt: FIXME: this is created before the call to Render::init. I will need to move some things around
    wnd_data->layer.device = MTLCreateSystemDefaultDevice();
    wnd_data->layer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    wnd_data->layer.framebufferOnly = YES;
    wnd_data->layer.maximumDrawableCount = 2;
    ns_win.contentView.layer = wnd_data->layer;
    bool good = true;
    return good;
  }

  void os_swap_buffers(OS::OSWindow)
  {
    MetalWindowData *wnd_data = metal_wind_backend_data();
    MetalRenderData *rend_data = metal_render_backend_data();
    [wnd_data->cmd_buffer presentDrawable:wnd_data->drawable];
    [wnd_data->cmd_buffer commit];
    [wnd_data->cmd_buffer waitUntilCompleted];
  }

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
    MetalRenderData *rend_data = metal_render_backend_data();
    auto scratch = Arena::scratch_begin(Arena::no_conflicts);
    bool good = true;
    rend_data->mtl_device = MTLCreateSystemDefaultDevice();
    rend_data->mtl_command_queue = [rend_data->mtl_device newCommandQueue];

    @autoreleasepool
    {
      //- brt: compile shaders
      id<MTLLibrary> mtl_library = nil;
      {
        auto tmp = Arena::temp_begin(scratch.arena);
        Assets::AssetID asset_id = Assets::AssetID::ShadersMSL;
        Assets::AssetBuffer ass_buf = {};
        ass_buf.len = rep(Assets::asset_length(asset_id));
        ass_buf.buf = Arena::push_array_no_zero<uint8_t>(tmp.arena, ass_buf.len);
        if (not Assets::populate_asset(ass_buf, asset_id))
        {
          Assets::AssetDescription desc = Assets::describe(asset_id);
          String8 msg = str8_fmt(scratch.arena, "Failed to load shader asset: '%S'", desc.proxy_file);
          fprintf(stderr, "%.*s\n", static_cast<int>(msg.size), msg.str);
          good = false;
        }
        if (good)
        {
          NSError *error = nil;
          NSString *shaders_src = ns_string_from_str8(str8((char *)ass_buf.buf, ass_buf.len));
          MTLCompileOptions *options = [[[MTLCompileOptions alloc] init] autorelease];
          mtl_library = [rend_data->mtl_device newLibraryWithSource:shaders_src
                                                            options:options
                                                              error:&error];
          if (error)
          {
            Assets::AssetDescription desc = Assets::describe(asset_id);
            String8 reason = str8_cstr((char *)[[error localizedDescription] cStringUsingEncoding:NSUTF8StringEncoding]);
            String8 msg = str8_fmt(scratch.arena, "Failed to compile shader file '%s': '%S'", desc.proxy_file, reason);
            fprintf(stderr, "%.*s\n", static_cast<int>(msg.size), msg.str);
            good = false;
          }
        }
        Arena::temp_end(tmp);
      }

      //- brt: create PSOs
      id<MTLRenderPipelineState> pipelines[rep(FragShader::Count)] = {};
      if (good)
        for EachIndex(frag_idx, rep(FragShader::Count))
      {
        NSString *frag_name = nil;
        switch (FragShader(frag_idx))
        {
          case FragShader::Image:         frag_name = @"fs_image"; break;
          case FragShader::Text:          frag_name = @"fs_text"; break;
          case FragShader::TextSubpixel:  frag_name = @"fs_text_subpixel"; break;
          case FragShader::BlurHorizVert: frag_name = @"fs_blur_horiz"; break;
        }

        MTLRenderPipelineDescriptor *mtl_pipeline_state_desc = [[MTLRenderPipelineDescriptor alloc] init];
        mtl_pipeline_state_desc.vertexFunction = [mtl_library newFunctionWithName:@"vs_main"];
        mtl_pipeline_state_desc.fragmentFunction = [mtl_library newFunctionWithName:frag_name];
        mtl_pipeline_state_desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
        mtl_pipeline_state_desc.vertexBuffers[0].mutability = MTLMutabilityImmutable;

        NSError *error = nil;
        pipelines[frag_idx] = [rend_data->mtl_device newRenderPipelineStateWithDescriptor:mtl_pipeline_state_desc error:&error];
        if (error)
        {
          String8 reason = str8_cstr((char *)[[error localizedDescription] cStringUsingEncoding:NSUTF8StringEncoding]);
          String8 msg = str8_fmt(scratch.arena, "Failed to create render pipeline: '%S'", reason);
          fprintf(stderr, "%.*s\n", static_cast<int>(msg.size), msg.str);
          good = false;
        }
      }
    }

    if (good)
    {
      screen_resize(screen);
    }
    Arena::scratch_end(scratch);
    return good;
  }

  BasicTexture create_basic_texture(const ScreenDimensions& size)
  {
    MetalRenderData *rend_data = metal_render_backend_data();

    //- brt: allocate
    MetalEntity *tex = push_render_entity(rend_data, &rend_data->tex_list);

    //- brt: create texture
    MTLTextureDescriptor *desc =
      [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                         width:rep(size.width)
                                                        height:rep(size.height)
                                                     mipmapped:NO];
    // brt: NOTE: this is works on Apple CPUs
    desc.storageMode = MTLStorageModeShared;
    tex->texture = [rend_data->mtl_device newTextureWithDescriptor:desc];
    return to_basic_texture(tex);
  }

  void delete_basic_texture(BasicTexture tex)
  {
    MetalRenderData *data = metal_render_backend_data();
    MetalEntity *entity_tex = basic_texture_metal(tex);
    [entity_tex->texture release];
    entity_tex->texture = nil;
    release_render_entity(data, &data->tex_list, entity_tex);
  }

  void submit_basic_texture_data(BasicTexture tex, BasicTextureEntry entry)
  {
    MetalEntity *entity_tex = basic_texture_metal(tex);
    MTLRegion region = MTLRegionMake2D(rep(entry.offset_x),
                                       rep(entry.offset_y),
                                       rep(entry.width),
                                       rep(entry.height));
    if (rep(entry.width) > 0 && rep(entry.height) > 0)
    {
      [entity_tex->texture replaceRegion:region
                             mipmapLevel:0
                               withBytes:entry.buffer
                             bytesPerRow:rep(entry.width) * 4];
    }
  }

  BasicTexture create_glyph_texture(const ScreenDimensions& dim)
  {
    MetalRenderData *rend_data = metal_render_backend_data();

    //- brt: allocate
    MetalEntity *tex = push_render_entity(rend_data, &rend_data->tex_list);

    //- brt: create texture
    MTLTextureDescriptor *desc =
      [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                         width:rep(dim.width)
                                                        height:rep(dim.height)
                                                     mipmapped:NO];
    // brt: NOTE: this is works on Apple CPUs
    desc.storageMode = MTLStorageModeShared;
    tex->texture = [rend_data->mtl_device newTextureWithDescriptor:desc];
    return to_basic_texture(tex);
  }

  void submit_glyph_data(BasicTexture tex, GlyphEntry entry)
  {
    MetalEntity *entity_tex = basic_texture_metal(tex);
    MTLRegion region = MTLRegionMake2D(rep(entry.offset_x),
                                       rep(entry.offset_y),
                                       rep(entry.width),
                                       rep(entry.height));
    if (rep(entry.width) > 0 && rep(entry.height) > 0)
    {
      [entity_tex->texture replaceRegion:region
                             mipmapLevel:0
                               withBytes:entry.buffer
                             bytesPerRow:rep(entry.width) * 4];
    }
  }

  void request_frames()
  {
    MetalRenderData *data = metal_render_backend_data();
    data->requested_frames = 4;
  }

  int frames_remaining()
  {
    MetalRenderData *data = metal_render_backend_data();
    return data->requested_frames;
  }

  void consume_frame()
  {
    MetalRenderData *data = metal_render_backend_data();
    if (data->requested_frames > 0)
    {
      --data->requested_frames;
    }
  }

  void draw_cmd_list(FrameRenderer*, CmdBuffer::CmdList* cmd_lst)
  {
    MetalWindowData *wnd_data = metal_wind_backend_data();
    MetalRenderData *rend_data = metal_render_backend_data();

    wnd_data->drawable = [wnd_data->layer nextDrawable];
    id<MTLDevice> mtl_device = rend_data->mtl_device;
    id<MTLCommandBuffer> mtl_cmd_buffer = [rend_data->mtl_command_queue commandBuffer];
    wnd_data->cmd_buffer = mtl_cmd_buffer;
    MTLRenderPassDescriptor *mtl_pass_desc = [MTLRenderPassDescriptor renderPassDescriptor];
    mtl_pass_desc.colorAttachments[0].texture = wnd_data->stage_color;
    mtl_pass_desc.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    mtl_pass_desc.colorAttachments[0].storeAction = MTLStoreActionDontCare;
    id<MTLRenderCommandEncoder> mtl_encoder = [mtl_cmd_buffer renderCommandEncoderWithDescriptor:mtl_pass_desc];

    // brt: setup primary viewport
    MTLViewport viewport =
    {
      0.0, 0.0,
      static_cast<float>(rep(rend_data->screen_size.width)),
      static_cast<float>(rep(rend_data->screen_size.height)),
      0.0, 1.0
    };
    [mtl_encoder setViewport:viewport];

    for EachIndex(idx, count_of<CmdBuffer::DrawListLayer>)
    {
      CmdBuffer::DrawListCollection* lst_c = &cmd_lst->draw_list[idx];
      for EachNode(lst_n, lst_c->first)
      {
        CmdBuffer::DrawList* lst = lst_n->lst;

        ScreenDimensions res = lst->screen;
        for EachNode(cmd_n, lst->cmd_buf.first)
        {
          const CmdBuffer::DrawCmd *cmd = &cmd_n->cmd;
          switch(cmd->sort)
          {
            case CmdBuffer::CmdSort::Standard:
            {
            }break;
            case CmdBuffer::CmdSort::Blur:
            {
            }break;
            case CmdBuffer::CmdSort::CameraUpdate:
            {
            }break;
            case CmdBuffer::CmdSort::ResolutionUpdate:
            {
            }break;
          }
        }
      }
    }

    [mtl_encoder endEncoding];
  }

  void apply_framebuffer(Render::FrameRenderer*, const ScreenDimensions& screen)
  {
    //- brt: NYI
  }

  void window_end_frame(OS::OSWindow window)
  {
    MetalRenderData *rend_data = metal_render_backend_data();
    for (;rend_data->flush_buffer_list.first != nullptr;)
    {
      MetalEntity *n = rend_data->flush_buffer_list.first;
      [n->vertex_buffer release];
      [n->index_buffer release];
      release_render_entity(rend_data, &rend_data->flush_buffer_list, n);
    }
    rend_data->flush_buffer_list = {};
    OS::swap_buffers(window);
  }

  void screen_resize(const ScreenDimensions& screen)
  {
    MetalWindowData *wnd_data = metal_wind_backend_data();
    MetalRenderData *rend_data = metal_render_backend_data();

    //- brt: destroy old resources
    [wnd_data->scratch_color release];
    [wnd_data->stage_color release];
    wnd_data->scratch_color = nil;
    wnd_data->stage_color = nil;

    //- brt: create new resources
    MTLTextureDescriptor *color_desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                                                          width:(NSUInteger)screen.width
                                                                                         height:(NSUInteger)screen.height
                                                                                      mipmapped:NO];
    color_desc.textureType = MTLTextureType2D;
    color_desc.usage |= MTLTextureUsageRenderTarget;
    color_desc.storageMode = MTLStorageModeMemoryless;
    wnd_data->scratch_color = [rend_data->mtl_device newTextureWithDescriptor:color_desc];
    wnd_data->stage_color = [rend_data->mtl_device newTextureWithDescriptor:color_desc];

    //- brt: resize drawable
    rend_data->screen_size = screen;
    wnd_data->layer.drawableSize = CGSizeMake(rep(screen.width), rep(screen.height));
  }

  bool init_renderer_arenas()
  {
    MetalRenderData *rend_data = metal_render_backend_data();
    rend_data->render_arena = Arena::alloc(Arena::default_params);
    return true;
  }

  void display_renderer_version()
  {
    printf("Metal\n");
  }
}
