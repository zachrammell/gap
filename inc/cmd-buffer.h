#pragma once

#include "arena.h"
#include "types.h"
#include "vec.h"

namespace CmdBuffer
{
    enum class CmdSort : uint32_t
    {
        Standard,
        Blur,
        CameraUpdate,
        ResolutionUpdate,
    };

    struct StandardData
    {
        Render::BasicTexture tex;
        uint8_t pad0;
        Render::BlendingMode blend;
        Render::VertShader vert;
        Render::FragShader frag;
        VertexOffset vert_off; // Start offset into the vertex buffer.
        IndexOffset idx_off;   // Start offset into the index buffer.
        Cardinality idx_count; // Multiples of 3 because... vertices.
    };

    struct DrawCmd
    {
        CmdSort sort;
        uint32_t pad0;
        ClipRect clip_rect;
        union
        {
            StandardData std_data;
            Render::Camera camera_up;
            ScreenDimensions res_up;
        };
    };

    static_assert(sizeof(StandardData) == 24);
    static_assert(sizeof(DrawCmd) == 56);
    static_assert(alignof(DrawCmd) == 8);

    struct DrawVertex
    {
        Vec2f pos;
        Vec4f color;
        Vec2f uv;
        float cust1; // Currently the offset for subpixel shift.
        float cust2;
    };

    struct BatchedTexture
    {
        Render::BasicTexture tex;
        Vec2f pos;
        Vec2f size;
        Vec2f uv_start;
        Vec2f uv_end;
        Vec4f color;
    };

    enum class Index : uint32_t { };

    struct IndexBuffer
    {
        Index* buf;
        uint64_t count;
    };

    struct VertexBuffer
    {
        DrawVertex* buf;
        uint64_t count;
    };

    struct DrawListEntityNode
    {
        DrawListEntityNode* next;
        union
        {
            DrawCmd cmd;
            ClipRect clip_rect;
            Render::BasicTexture tex;
            BatchedTexture batch_tex;
            ColorPalette color_palette;
        };
    };

    struct DrawListEntityList
    {
        DrawListEntityNode* first;
        DrawListEntityNode* last;
        uint64_t count;
    };

    struct DrawList
    {
        IndexBuffer idx_buf;
        VertexBuffer vert_buf;
        DrawListEntityList cmd_buf;
        DrawListEntityList clip_rects;
        DrawListEntityList texture_stack;
        DrawListEntityList texture_batch;
        DrawListEntityList color_palettes;

        // Shared free list.
        DrawListEntityNode* free_entity_list;

        // Arena for lists above.
        Arena::Arena* entity_list_arena;
        Arena::Arena* vert_buf_arena;
        Arena::Arena* idx_buf_arena;

        DrawCmd current;
        Index last_index;
        ScreenDimensions screen;
        float delta_time;
        float app_time;
    };

    struct DrawListCollectionNode
    {
        DrawListCollectionNode* next;
        DrawList* lst;
    };

    struct DrawListCollection
    {
        DrawListCollectionNode* first;
        DrawListCollectionNode* last;
        uint64_t count;
    };

    struct CmdList
    {
        DrawListCollection draw_list;
        Arena::Arena* cmd_list_arena;
    };

    // For draw list allocation API.
    struct DrawListAllocNode
    {
        DrawListAllocNode* next;
        DrawListAllocNode* prev;
        DrawList lst;
    };

    struct DrawListAllocList
    {
        DrawListAllocNode* first;
        DrawListAllocNode* last;
        uint64_t count;
    };

    struct DrawListAllocator
    {
        DrawListAllocList alive;
        DrawListAllocList dead;
        DrawListAllocNode* free_lst;
        Arena::Arena* alloc_arena;
    };
} // namespace CmdBuffer