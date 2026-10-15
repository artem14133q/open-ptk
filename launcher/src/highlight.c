// highlight.c
//
// EN: The yellow outline round the thing under the pointer, if it can be clicked.
// RU: Жёлтая обводка вокруг предмета под указателем, если с ним можно взаимодействовать.
#include "game.h"

#include "rlgl.h"

// The outline is OUTLINE game pixels wide: the first ring bright, the next
// fainter. The quad is drawn that much bigger than the clip's texture; out
// of the texture counts as clear.
#define OUTLINE 2

#if defined(PLATFORM_NX) || defined(GRAPHICS_API_OPENGL_ES2)
#define OUTLINE_HEAD                                                               \
    "#version 100\n"                                                              \
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n" \
    "varying vec2 fragTexCoord;\nvarying vec4 fragColor;\n"                       \
    "#define TEX texture2D\n#define OUT gl_FragColor\n"
#else
#define OUTLINE_HEAD                                                               \
    "#version 330\n"                                                              \
    "in vec2 fragTexCoord;\nin vec4 fragColor;\nout vec4 finalColor;\n"           \
    "#define TEX texture\n#define OUT finalColor\n"
#endif

static const char kOutlineBody[] =
    "uniform sampler2D texture0;\n"
    "uniform vec2 texel;\n"   // one texel of the clip's texture, in texture coordinates
    "uniform vec4 ink;\n"
    "float alpha_at(vec2 uv) {\n"
    "    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) return 0.0;\n"
    "    return TEX(texture0, uv).a;\n"
    "}\n"
    "void main() {\n"
    "    vec2 uv = fragTexCoord;\n"
    "    if (alpha_at(uv) > 0.5) discard;\n"   // the thing itself is drawn as it is
    "    float near = 0.0, far = 0.0;\n"
    "    for (int y = -2; y <= 2; y++) {\n"
    "        for (int x = -2; x <= 2; x++) {\n"
    "            float a = alpha_at(uv + vec2(float(x), float(y)) * texel);\n"
    "            if (abs(x) <= 1 && abs(y) <= 1) near = max(near, a);\n"
    "            else far = max(far, a);\n"
    "        }\n"
    "    }\n"
    "    float strength = near > 0.5 ? 1.0 : far > 0.5 ? 0.45 : 0.0;\n"
    "    if (strength == 0.0) discard;\n"
    "    OUT = vec4(ink.rgb, ink.a * strength);\n"
    "}\n";

static Shader s_shader;
static int s_loc_texel = -1, s_loc_ink = -1;

static bool shader_ok(void) { return IsShaderValid(s_shader) && s_shader.id != rlGetShaderIdDefault(); }

void highlight_init(void) {
    static char source[sizeof(OUTLINE_HEAD) + sizeof(kOutlineBody)];
    snprintf(source, sizeof(source), "%s%s", OUTLINE_HEAD, kOutlineBody);
    s_shader = LoadShaderFromMemory(NULL, source);
    if (!shader_ok()) return;
    s_loc_texel = GetShaderLocation(s_shader, "texel");
    s_loc_ink = GetShaderLocation(s_shader, "ink");
}

void highlight_close(void) {
    if (shader_ok()) UnloadShader(s_shader);
}

// A way out: walking to it takes the heroes to another room -- a WALK
// reaction with a GOTO in it.
static int is_exit(const PetkaObject *obj) {
    for (uint32_t r = 0; r < obj->reaction_count; r++) {
        const PetkaReaction *re = &obj->reactions[r];
        if (re->opcode != PETKA_OP_WALK) continue;
        for (uint32_t m = 0; m < re->message_count; m++)
            if (re->messages[m].opcode == PETKA_OP_GOTO) return 1;
    }
    return 0;
}

int highlight_wanted(const Game *g, const PetkaObject *obj) {
    if (is_hero(obj->id) || is_exit(obj)) return 0;
    // Nor the other characters: CAST.INI gives the lines of everyone who
    // speaks a colour of their own.
    Color ink = g->colors[obj_index(g, obj)], none = NO_CAST_INK;
    if (ink.r != none.r || ink.g != none.g || ink.b != none.b) return 0;
    // Seen or not: a hidden one that can be clicked is a thing painted on
    // the background, and its clip is that thing cut out of it.
    return g->vm->state[obj_index(g, obj)].active;
}

void highlight_draw(const Visual *v, Rectangle frame) {
    if (!shader_ok() || !v->tex.id || !v->flic) return;
    float sx = frame.width / v->flic->width, sy = frame.height / v->flic->height;
    float tw = (float)v->tex.width, th = (float)v->tex.height;
    float texel[2] = {1.0f / tw, 1.0f / th};
    float ink[4] = {1.0f, 0.86f, 0.1f, 0.95f};
    BeginShaderMode(s_shader);
    SetShaderValue(s_shader, s_loc_texel, texel, SHADER_UNIFORM_VEC2);
    SetShaderValue(s_shader, s_loc_ink, ink, SHADER_UNIFORM_VEC4);
    DrawTexturePro(v->tex, (Rectangle){-OUTLINE, -OUTLINE, tw + 2 * OUTLINE, th + 2 * OUTLINE},
                   (Rectangle){frame.x + (v->area.left - OUTLINE) * sx, frame.y + (v->area.top - OUTLINE) * sy,
                               (tw + 2 * OUTLINE) * sx, (th + 2 * OUTLINE) * sy},
                   (Vector2){0, 0}, 0, WHITE);
    EndShaderMode();
}
