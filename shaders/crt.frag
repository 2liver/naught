#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag;
layout(binding = 1) uniform sampler2D tex;
layout(std140, binding = 0) uniform buf {
    vec2 view;
    vec2 texSize;
} ubuf;

vec2 curve(vec2 uv) {
    vec2 c = uv - 0.5;
    c += ubuf.view * 0.055;
    float r2 = dot(c, c);
    return c * (1.0 + 0.14 * r2) + 0.5;
}

void main()
{
    vec2 uv = curve(v_uv);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        frag = vec4(0.012, 0.009, 0.004, 1.0);
        return;
    }
    float sub = fract(uv.x * ubuf.texSize.x * 3.0);
    float rMask = smoothstep(0.0, 0.333, sub) * (1.0 - smoothstep(0.333, 0.667, sub));
    float gMask = smoothstep(0.333, 0.667, sub) * (1.0 - smoothstep(0.667, 1.0, sub));
    float bMask = smoothstep(0.667, 1.0, sub);
    vec3 texcol = texture(tex, uv).rgb;
    vec3 phos = vec3(texcol.r, texcol.g * 0.69, texcol.b * 0.06);
    vec3 col = vec3(phos.r * rMask + phos.g * gMask + phos.b * bMask) * 3.0;
    float row = fract(uv.y * ubuf.texSize.y);
    col *= 1.0 - 0.22 * step(0.5, fract(row * 0.5));
    vec2 n = normalize(vec2(ubuf.view.x * 0.8, 0.6));
    float refl = pow(max(0.0, 1.0 - abs(dot(n, vec2(0.35, 0.94)) - 0.62) * 3.2), 2.0);
    col += vec3(1.0, 0.88, 0.62) * refl * 0.055;
    float d = length(uv - 0.5) * 1.7;
    col *= 1.0 - 0.45 * smoothstep(0.4, 1.0, d);
    frag = vec4(clamp(col, 0.0, 1.0), 1.0);
}
