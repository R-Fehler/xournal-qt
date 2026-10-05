// xournal-qt: a tile of a page shown dark (DarkTileMaterial.h, the table: canvas/DarkPages.h). The tile's color,
// balanced by its paper, is looked up in the table (33 x 33 x 33 colors as 33 slices side by side), trilinear: the
// same as dark::lookup on the CPU. Pixels in the kept rectangles (pictures) stay as they are.
#version 440
layout(location = 0) in vec2 vTex;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float keepCount;
    vec4 balance;
    vec4 keep[8];  // texture coordinates: x0, y0, x1, y1
};
layout(binding = 1) uniform sampler2D tileTex;
layout(binding = 2) uniform sampler2D lut;

const float N = 33.0;

void main() {
    vec4 c = texture(tileTex, vTex);
    bool kept = false;
    for (int i = 0; i < 8; ++i) {
        vec4 r = keep[i];
        if (float(i) < keepCount && vTex.x >= r.x && vTex.y >= r.y && vTex.x < r.z && vTex.y < r.w)
            kept = true;
    }
    if (!kept && c.a > 0.0) {
        vec3 rgb = clamp(c.rgb / c.a * balance.rgb, 0.0, 1.0) * (N - 1.0);
        float z0 = min(floor(rgb.b), N - 2.0);
        float fz = rgb.b - z0;
        // (half a texel in: the middle of the table's cells)
        vec2 uv = vec2((rgb.r + 0.5) / (N * N), (rgb.g + 0.5) / N);
        vec3 c0 = texture(lut, uv + vec2(z0 / N, 0.0)).rgb;
        vec3 c1 = texture(lut, uv + vec2((z0 + 1.0) / N, 0.0)).rgb;
        c.rgb = mix(c0, c1, fz) * c.a;
    }
    fragColor = c * qt_Opacity;
}
