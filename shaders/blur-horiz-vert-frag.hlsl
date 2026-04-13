cbuffer Uniforms : register(b0)
{
    float2 resolution;
    float time;
    float camera_coord_factor;
    float2 camera_scale;
    float2 camera_pos;
    float2 custom_vec2_value1;
    float2 custom_vec2_value2;
    float2 custom_vec2_value3;
};

struct PS_INPUT
{
    float4 pos                  : SV_POSITION;
    float4 color                : COL0;
    float2 uv                   : TEX;
    float4 premul_color         : COL1;
    nointerpolation float cust1 : CUST0; // Blur horiz == 0.0, blur vert == 1.0.
    nointerpolation float cust2 : CUST1;
};

sampler sampler0;
Texture2D texture0;

// Higher value, more centered glow.
// Lower values might need more taps.
#define GLOW_FALLOFF custom_vec2_value1.x
#define TAPS int(custom_vec2_value1.y)
#define original_resolution custom_vec2_value2

#define kernel(x) exp(-GLOW_FALLOFF * (x) * (x))

float4 blur(float2 tex, float2 texture_size, float flags)
{
    float4 col = float4(0.0, 0.0, 0.0, 0.0);
    float dx = 1.0 / texture_size.x;
    float dy = 1.0 / texture_size.y;
    dx *= float(flags == 0.0);
    dy *= float(flags > 0.0);

    float k_total = 0.0;
    for (int i = -TAPS; i <= TAPS; i++)
    {
        float k = kernel(i);
        k_total += k;
        col += k * texture0.Sample(sampler0, tex + float2(float(i) * dx, float(i) * dy));
    }
    return float4(col / k_total);
}

float4 main(PS_INPUT input) : SV_Target
{
    float4 out_col = blur(input.uv, original_resolution, input.cust1);
    return out_col;
}