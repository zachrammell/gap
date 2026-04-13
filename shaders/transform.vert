#version 330 core

uniform vec2 resolution;
uniform float time;
uniform float camera_coord_factor;
uniform vec2 camera_scale;
uniform vec2 camera_pos;
uniform vec2 custom_vec2_value1;
uniform vec2 custom_vec2_value2;
uniform vec2 custom_vec2_value3;

layout(location = 0) in vec2 position;
layout(location = 1) in vec4 color;
layout(location = 2) in vec2 uv;
layout(location = 3) in float cust1;
layout(location = 4) in float cust2;

out vec4 out_color;
out vec2 out_uv;
out float out_cust1;
out float out_cust2;
out vec2 transformed_custom_vec2_value1;
out vec2 transformed_custom_vec2_value2;
out vec2 transformed_custom_vec2_value3;
out vec2 vert_pos;
out vec4 out_premul_color;

vec2 camera_project(vec2 point) {
#if 1
    vec2 scale = camera_coord_factor * camera_scale.x / resolution;
    // We're going to snap to pixel edges rather than arbitrary pixel positions.
    vec2 offset = -scale * camera_pos;
    vec2 pixel_size = camera_coord_factor / resolution;
    offset = round(offset/pixel_size)*pixel_size;
    return scale*point + offset;
#endif
    return camera_coord_factor * (point - camera_pos) * camera_scale.x / resolution;
}

void main() {
    gl_Position = vec4(camera_project(position), 0, 1);
    out_color = color;
    out_premul_color = vec4(color.rgb * color.a, color.a);
    out_uv = uv;
    out_cust1 = cust1;
    out_cust2 = cust2;
    transformed_custom_vec2_value1 = camera_project(custom_vec2_value1);
    transformed_custom_vec2_value2 = camera_project(custom_vec2_value2);
    transformed_custom_vec2_value3 = camera_project(custom_vec2_value3);
    vert_pos = camera_project(position);
}
