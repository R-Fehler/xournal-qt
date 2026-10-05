// xournal-qt: a tile of a page shown dark (DarkTileMaterial.h)
#version 440
layout(location = 0) in vec4 qt_VertexPosition;
layout(location = 1) in vec2 qt_VertexTexCoord;
layout(location = 0) out vec2 vTex;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float keepCount;
    vec4 balance;
    vec4 keep[8];
};
void main() {
    vTex = qt_VertexTexCoord;
    gl_Position = qt_Matrix * qt_VertexPosition;
}
