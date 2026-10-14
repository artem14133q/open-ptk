#include "crt.h"

#include "rlgl.h"

#include <stdio.h>
#include <stdlib.h>

// raylib's default vertex shader hands on fragTexCoord and fragColor; the
// Switch's raylib runs on OpenGL ES 2 (GLSL 100), a desktop's on OpenGL 3.3.
#if defined(PLATFORM_NX) || defined(GRAPHICS_API_OPENGL_ES2)
#define CRT_HEAD                                                                    \
    "#version 100\n"                                                               \
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n" \
    "varying vec2 fragTexCoord;\nvarying vec4 fragColor;\n"                        \
    "#define TEX texture2D\n#define OUT gl_FragColor\n"
#else
#define CRT_HEAD                                                                    \
    "#version 330\n"                                                               \
    "in vec2 fragTexCoord;\nin vec4 fragColor;\nout vec4 finalColor;\n"            \
    "#define TEX texture\n#define OUT finalColor\n"
#endif

static const char kCrtBody[] =
    "uniform sampler2D texture0;\n"
    // The game's own lines and columns, whatever the picture is drawn at.
    "const vec2 GAME = vec2(640.0, 480.0);\n"
    "vec2 bulge(vec2 uv) {\n"
    "    uv = uv * 2.0 - 1.0;\n"
    "    vec2 off = abs(uv.yx) / vec2(7.0, 5.5);\n"
    "    uv = uv + uv * off * off;\n"
    "    return uv * 0.5 + 0.5;\n"
    "}\n"
    "void main() {\n"
    "    vec2 uv = bulge(fragTexCoord);\n"
    "    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) { OUT = vec4(0.0, 0.0, 0.0, 1.0); return; }\n"
    // The beam is a little wider than a pixel: a touch of its neighbours.
    "    vec2 px = vec2(1.0 / GAME.x, 0.0);\n"
    "    vec3 col = TEX(texture0, uv).rgb * 0.6\n"
    "             + TEX(texture0, uv - px * 0.6).rgb * 0.2\n"
    "             + TEX(texture0, uv + px * 0.6).rgb * 0.2;\n"
    // Scanlines: bright at the middle of each of the 480 lines, dark between,
    // and a bright line spreads wider.
    "    float f = fract(uv.y * GAME.y) - 0.5;\n"
    "    float lum = dot(col, vec3(0.30, 0.59, 0.11));\n"
    "    float w = mix(0.24, 0.42, lum);\n"
    "    col *= mix(0.45, 1.0, exp(-(f * f) / (2.0 * w * w)));\n"
    // The phosphor's stripes, one screen pixel each.
    "    float m = mod(gl_FragCoord.x, 3.0);\n"
    "    vec3 mask = m < 1.0 ? vec3(1.0, 0.82, 0.82) : m < 2.0 ? vec3(0.82, 1.0, 0.82) : vec3(0.82, 0.82, 1.0);\n"
    "    col *= mask;\n"
    // Darker towards the corners, and the light lost brought back.
    "    float v = 16.0 * uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y);\n"
    "    col *= pow(v, 0.12) * 1.32;\n"
    "    OUT = vec4(col, 1.0);\n"
    "}\n";

static Shader s_shader;
static bool s_on;

void crt_init(void) {
    static char source[sizeof(CRT_HEAD) + sizeof(kCrtBody)];
    snprintf(source, sizeof(source), "%s%s", CRT_HEAD, kCrtBody);
    s_shader = LoadShaderFromMemory(NULL, source);
    const char *env = getenv("PETKA_CRT");
    s_on = env ? atoi(env) != 0 : !(getenv("PETKA_SHOT") || getenv("PETKA_PLAY") || getenv("PETKA_TURBO"));
    // A shader that did not build comes back as raylib's default one.
    if (!IsShaderValid(s_shader) || s_shader.id == rlGetShaderIdDefault()) s_on = false;
}

void crt_close(void) {
    if (IsShaderValid(s_shader) && s_shader.id != rlGetShaderIdDefault()) UnloadShader(s_shader);
}

void crt_toggle(void) {
    if (IsShaderValid(s_shader) && s_shader.id != rlGetShaderIdDefault()) s_on = !s_on;
}

bool crt_on(void) { return s_on; }

void crt_draw(Texture2D tex, Rectangle src, Rectangle dst) {
    if (s_on) BeginShaderMode(s_shader);
    DrawTexturePro(tex, src, dst, (Vector2){0, 0}, 0, WHITE);
    if (s_on) EndShaderMode();
}
