// glshader_probe.cpp —— 无窗口编译 CRT 着色器并打印日志（诊断工具）
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <cstdio>

static const char *kFrag = R"GLSL(
#version 330 core
in vec2 v_uv;
out vec4 frag;
uniform sampler2D tex;
uniform vec2 view;
uniform vec2 texSize;

vec2 curve(vec2 uv) {
    vec2 c = uv - 0.5;
    c += view * 0.055;
    float r2 = dot(c, c);
    return c * (1.0 + 0.14 * r2) + 0.5;
}

void main() {
    vec2 uv = curve(v_uv);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        frag = vec4(0.012, 0.009, 0.004, 1.0);
        return;
    }
    vec2 px = 1.0 / texSize;
    float sub = fract(uv.x * texSize.x * 3.0);
    float rMask = smoothstep(0.0, 0.333, sub) * (1.0 - smoothstep(0.333, 0.667, sub));
    float gMask = smoothstep(0.333, 0.667, sub) * (1.0 - smoothstep(0.667, 1.0, sub));
    float bMask = smoothstep(0.667, 1.0, sub);
    vec3 texcol = texture(tex, uv).rgb;
    vec3 phos = vec3(texcol.r, texcol.g * 0.69, texcol.b * 0.06);
    vec3 col = vec3(phos.r * rMask + phos.g * gMask + phos.b * bMask) * 3.0;
    float row = fract(uv.y * texSize.y);
    col *= 1.0 - 0.22 * step(0.5, fract(row * 0.5));
    vec2 n = normalize(vec2(view.x * 0.8, 0.6));
    float refl = pow(max(0.0, 1.0 - abs(dot(n, vec2(0.35, 0.94)) - 0.62) * 3.2), 2.0);
    col += vec3(1.0, 0.88, 0.62) * refl * 0.055;
    float d = length(uv - 0.5) * 1.7;
    col *= 1.0 - 0.45 * smoothstep(0.4, 1.0, d);
    frag = vec4(clamp(col, 0.0, 1.0), 1.0);
}
)GLSL";

static const char *kVert = R"GLSL(
#version 330 core
in vec2 aPos;
in vec2 aUv;
out vec2 v_uv;
void main() {
    v_uv = aUv;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QOffscreenSurface surf;
    surf.setFormat(fmt);
    surf.create();
    if (!surf.isValid()) {
        printf("FAIL: offscreen surface invalid\n");
        return 1;
    }
    QOpenGLContext ctx;
    ctx.setFormat(fmt);
    if (!ctx.create()) {
        printf("FAIL: context create failed\n");
        return 1;
    }
    if (!ctx.makeCurrent(&surf)) {
        printf("FAIL: makeCurrent failed\n");
        return 1;
    }
    printf("GL: %s %s\n", (const char *)ctx.functions()->glGetString(GL_VERSION),
           (const char *)ctx.functions()->glGetString(GL_RENDERER));
    QOpenGLShaderProgram prog;
    if (!prog.addShaderFromSourceCode(QOpenGLShader::Vertex, kVert))
        printf("VERT FAIL: %s\n", prog.log().toUtf8().constData());
    if (!prog.addShaderFromSourceCode(QOpenGLShader::Fragment, kFrag))
        printf("FRAG FAIL: %s\n", prog.log().toUtf8().constData());
    if (!prog.link())
        printf("LINK FAIL: %s\n", prog.log().toUtf8().constData());
    else
        printf("LINK OK\n");
    return 0;
}
