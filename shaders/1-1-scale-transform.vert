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

vec2 scale_pos(vec2 point) {
    vec2 scale = camera_coord_factor * 1.0 / resolution;
    // Now we're going to transform 'point' into device coords since it will come in
    // as screen-space coordinate (0,0) being lower LHS of the screen.
    // Note: we also 'floor' so we snap to pixel edges rather than arbitrary pixel positions.
    return (floor(point) * scale - 1.0);
}

void main() {
    gl_Position = vec4(scale_pos(position), 0, 1);
    out_color = color;
    out_premul_color = vec4(color.rgb * color.a, color.a);
    out_uv = uv;
    out_cust1 = cust1;
    out_cust2 = cust2;
    transformed_custom_vec2_value1 = scale_pos(custom_vec2_value1);
    transformed_custom_vec2_value2 = scale_pos(custom_vec2_value2);
    transformed_custom_vec2_value3 = scale_pos(custom_vec2_value3);
    vert_pos = scale_pos(position);
}