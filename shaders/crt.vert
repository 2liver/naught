#version 450
layout(location = 0) out vec2 v_uv;
void main()
{
    vec2 pos = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    // 采样约定与渲染目标写入相反（上传/回读均为行 0 = 顶，采样 v=0 却
    // 读底行）——顶点统一翻转一次，所有通道的 texture() 全部归位
    v_uv = vec2(pos.x, 1.0 - pos.y);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
