#version 330 core

uniform vec2 resolution;
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

void main() {
    gl_Position = vec4(position / resolution, 0, 1);
    out_color = color;
    out_premul_color = vec4(color.rgb * color.a, color.a);
    out_uv = uv;
    out_cust1 = cust1;
    out_cust2 = cust2;
    transformed_custom_vec2_value1 = custom_vec2_value1 / resolution;
    transformed_custom_vec2_value2 = custom_vec2_value2 / resolution;
    transformed_custom_vec2_value3 = custom_vec2_value3 / resolution;
    vert_pos = position / resolution;
}