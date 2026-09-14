#version 450
layout(location = 0) out vec2 v_uv;
void main()
{
    vec2 pos = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    v_uv = pos; // 标准全屏三角形：顶点 uv=(0,0),(2,0),(0,2)，插值后屏幕内恰为 0~1
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
