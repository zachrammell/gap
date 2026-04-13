#include "renderer.h"

#include "constants.h"

namespace Render
{
    Camera cursor_camera_transform(const Camera& old_camera,
                                    Vec2f target,
                                    float target_scale_x,
                                    float zoom_factor_x,
                                    float delta_time)
    {
        Camera camera = old_camera;
        // Note: someday we may also change the y scale factor (which would require a corresponding
        // shader change), but not today.
        if (target_scale_x > Constants::max_camera_zoom)
        {
            target_scale_x = Constants::max_camera_zoom;
        }
        // Sometimes the camera will be set to a scale of 0.f to indicate that we're manually zooming.
        else if (camera.scale.x != 0.f)
        {
            float offset_x = target.x - zoom_factor_x/camera.scale.x;
            if (offset_x < 0.f)
            {
                offset_x = 0.f;
            }
            target.x = zoom_factor_x/camera.scale.x + offset_x;
        }

        // Let's try these faster values for a bit...
        camera.velocity = (target - camera.pos) * Vec2f(15.f);
        camera.scale_velocity.x = ((target_scale_x) - camera.scale.x) * 10.f;

        camera.pos = camera.pos + (camera.velocity * Vec2f(delta_time));
        camera.scale = camera.scale + camera.scale_velocity * delta_time;
        return camera;
    }

    WorldCamera cursor_camera_transform(const WorldCamera& old_camera,
                                    Vec2d target,
                                    double target_scale_x,
                                    double zoom_factor_x,
                                    float delta_time)
    {
        WorldCamera camera = old_camera;
        // Note: someday we may also change the y scale factor (which would require a corresponding
        // shader change), but not today.
        if (target_scale_x > Constants::max_camera_zoom)
        {
            target_scale_x = Constants::max_camera_zoom;
        }
        // Sometimes the camera will be set to a scale of 0. to indicate that we're manually zooming.
        else if (camera.scale.x != 0.)
        {
            double offset_x = target.x - zoom_factor_x/camera.scale.x;
            if (offset_x < 0.)
            {
                offset_x = 0.;
            }
            target.x = zoom_factor_x/camera.scale.x + offset_x;
        }

        // Let's try these faster values for a bit...
        camera.velocity = (target - camera.pos) * Vec2d(15.);
        camera.scale_velocity.x = ((target_scale_x) - camera.scale.x) * 10.;

        camera.pos = camera.pos + (camera.velocity * Vec2d(delta_time));
        camera.scale = camera.scale + camera.scale_velocity * delta_time;
        return camera;
    }

    Vec2f screen_to_world_transform(const Camera& camera,
                                    Vec2f point,
                                    const ScreenDimensions& screen)
    {
        // 'point' is assumed to be in screen coordinates.  In order to translate this to world
        // coordinates based on a specific camera, we need to compute the x/y plane coords first.
        const float x_coord = (2.f * (static_cast<float>(point.x) / rep(screen.width) - 0.f)) - 1.f;
        const float y_coord = 1.f - (2.f * (static_cast<float>(point.y) / rep(screen.height) - 0.f));

        // Now we perform the inverse of the vertex shader transform (see transform.vert for reference)
        // and offset by the camera offset.
        // Further note: we only populate the 'x' on the scale since we only scale by that factor for now.
        // See 'cursor_camera_transform'.
        point.x = camera.pos.x + ((x_coord * rep(screen.width)) / (Constants::shader_scale_factor * camera.scale.x));
        point.y = camera.pos.y + ((y_coord * rep(screen.height)) / (Constants::shader_scale_factor * camera.scale.x));
        return point;
    }

    RenderViewport RenderViewport::basic(const ScreenDimensions& screen)
    {
        return { .offset_x = ViewportOffsetX{ },
                    .offset_y = ViewportOffsetY{ },
                    .width = screen.width,
                    .height = screen.height };
    }
} // namespace Render