#include "shaders.hpp"
#include <EGL/egl.h>
#ifdef USE_GLES32
    #include <GLES3/gl32.h>
#else
    #include <GLES2/gl2.h>
    #include <GLES2/gl2ext.h>
#endif

#include <sparrow/nonstd/wlroots-full.hpp>
#include <vector>

const GLfloat quad_verts[8] = {
    1, -1, // top right
    -1, -1, // top left
    1, 1, // bottom right
    -1, 1, // bottom left
};

#ifdef USE_GLES32
static
const GLchar quad_vertex_src[] =
    R"glsl(
#version 320 es
in vec2 pos;
in vec2 texcoord;
out vec2 v_texcoord;

void main() {
    gl_Position = vec4(pos, 1.0, 1.0);
    v_texcoord = texcoord;
}
)glsl";

static const GLchar tex_fragment_src_rgbx[] =
    R"glsl(
#version 320 es
precision mediump float;
in vec2 v_texcoord;
uniform sampler2D tex;
out vec4 FragColor;

void main() {
    FragColor = texture(tex, v_texcoord);
}
)glsl";

    #ifdef UNUSED
static const GLchar tex_fragment_src_external[] =
    R"glsl(
#version 320 es
#extension GL_OES_EGL_image_external : require
precision mediump float;
in vec2 v_texcoord;
uniform samplerExternalOES tex;
out vec4 FragColor;

void main() {
    FragColor = textureExternal(tex, v_texcoord);
}
)glsl";
    #endif

static const GLchar rounded_vertex_src[] =
    R"glsl(
#version 320 es
in vec2 pos;
in vec2 texcoord;
out vec2 v_texcoord;
out vec2 v_pos;

void main() {
    gl_Position = vec4(pos, 0.0, 1.0);
    v_texcoord = texcoord;
    v_pos = pos;
}
)glsl";

static const GLchar rounded_fragment_src[] =
    R"glsl(
#version 320 es
precision highp float;
in vec2 v_texcoord;
in vec2 v_pos;
uniform sampler2D tex;
uniform float alpha;
uniform vec4 clip_rect;
uniform vec4 corner_radii;
uniform float output_height;
out vec4 FragColor;

float sdf_rounded_box(vec2 p, vec2 half_size, float r) {
    vec2 q = abs(p) - half_size + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main() {
    vec2 frag_pos = vec2(gl_FragCoord.x, output_height - gl_FragCoord.y);
    vec2 rect_min = clip_rect.xy;
    vec2 rect_size = clip_rect.zw;
    vec2 rect_center = rect_min + rect_size * 0.5;
    vec2 half_size = rect_size * 0.5;
    vec2 local_pos = frag_pos - rect_center;

    float r;
    if (local_pos.x < 0.0 && local_pos.y < 0.0) r = corner_radii.x;
    else if (local_pos.x >= 0.0 && local_pos.y < 0.0) r = corner_radii.y;
    else if (local_pos.x >= 0.0 && local_pos.y >= 0.0) r = corner_radii.z;
    else r = corner_radii.w;

    float d = sdf_rounded_box(local_pos, half_size, r);
    float aa = smoothstep(0.5, -0.5, d);

    vec4 color = texture(tex, v_texcoord);
    FragColor = vec4(color.rgb, color.a * alpha * aa);
}
)glsl";

#else
static
const GLchar quad_vertex_src[] =
    R"glsl(
attribute vec2 pos;
attribute vec2 texcoord;
varying vec2 v_texcoord;

void main() {
    gl_Position = vec4(pos, 1.0, 1.0);
    v_texcoord = texcoord;
}
)glsl";

static const GLchar tex_fragment_src_rgbx[] =
    R"glsl(
precision mediump float;
varying vec2 v_texcoord;
uniform sampler2D tex;

void main() {
    gl_FragColor = texture2D(tex, v_texcoord);
}
)glsl";

    #ifdef UNUSED
static const GLchar tex_fragment_src_external[] =
    R"glsl(
#extension GL_OES_EGL_image_external : require
precision mediump float;
varying vec2 v_texcoord;
uniform samplerExternalOES tex;

void main() {
    gl_FragColor = texture2D(tex, v_texcoord);
}
)glsl";
    #endif

static const GLchar rounded_vertex_src[] =
    R"glsl(
attribute vec2 pos;
attribute vec2 texcoord;
varying vec2 v_texcoord;
varying vec2 v_pos;

void main() {
    gl_Position = vec4(pos, 0.0, 1.0);
    v_texcoord = texcoord;
    v_pos = pos;
}
)glsl";

static const GLchar rounded_fragment_src[] =
    R"glsl(
precision highp float;
varying vec2 v_texcoord;
uniform sampler2D tex;
uniform float alpha;
uniform vec4 clip_rect;
uniform vec4 corner_radii;
uniform float output_height;

float sdf_rounded_box(vec2 p, vec2 half_size, float r) {
    vec2 q = abs(p) - half_size + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main() {
    vec2 frag_pos = vec2(gl_FragCoord.x, output_height - gl_FragCoord.y);
    vec2 rect_min = clip_rect.xy;
    vec2 rect_size = clip_rect.zw;
    vec2 rect_center = rect_min + rect_size * 0.5;
    vec2 half_size = rect_size * 0.5;
    vec2 local_pos = frag_pos - rect_center;

    float r;
    if (local_pos.x < 0.0 && local_pos.y < 0.0) r = corner_radii.x;
    else if (local_pos.x >= 0.0 && local_pos.y < 0.0) r = corner_radii.y;
    else if (local_pos.x >= 0.0 && local_pos.y >= 0.0) r = corner_radii.z;
    else r = corner_radii.w;

    float d = sdf_rounded_box(local_pos, half_size, r);
    float aa = smoothstep(0.5, -0.5, d);

    vec4 color = texture(tex, v_texcoord);
    gl_FragColor = vec4(color.rgb, color.a * alpha * aa);
}
)glsl";

#endif

GLuint compile_shader(GLuint type, const GLchar *src)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_FALSE)
    {
        GLint log_length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length <= 0)
        {
            log_length = 4096;
        }

        std::vector<GLchar> data(log_length + 1, '\0');
        GLsizei len = 0;
        glGetShaderInfoLog(shader, log_length, &len, data.data());

        printf("Shader compile error (%d): %.*s\n\n", len, len, data.data());
        printf("%s\n\n", src);

        glDeleteShader(shader);
        shader = 0;
    }

    return shader;
}

static GLuint link_program(const GLchar *vert_src, const GLchar *frag_src)
{
    GLuint vert = compile_shader(GL_VERTEX_SHADER, vert_src);
    if (!vert)
    {
        printf("Failed to compile vertex shader!\n");
        return 0;
    }

    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, frag_src);
    if (!frag)
    {
        printf("Failed to compile fragment shader!\n");
        glDeleteShader(vert);
        return 0;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);

    glDetachShader(prog, vert);
    glDetachShader(prog, frag);
    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok == GL_FALSE)
    {
        glDeleteProgram(prog);
        printf("Failed to link shader!\n");
        return 0;
    }

    return prog;
}

struct quad_rgbx_shader make_quad_rgbx_shader()
{
    GLuint prog = link_program(quad_vertex_src, tex_fragment_src_rgbx);

    struct quad_rgbx_shader shader = {};
    shader.prog  = prog;
    shader.proj  = glGetUniformLocation(prog, "proj");
    shader.tex   = glGetUniformLocation(prog, "tex");
    shader.alpha = glGetUniformLocation(prog, "alpha");
    shader.pos_attrib = glGetAttribLocation(prog, "pos");
    shader.tex_attrib = glGetAttribLocation(prog, "texcoord");

    return shader;
}

struct quad_rounded_shader make_quad_rounded_shader()
{
    GLuint prog = link_program(rounded_vertex_src, rounded_fragment_src);

    struct quad_rounded_shader shader = {};
    shader.prog  = prog;
    shader.tex   = glGetUniformLocation(prog, "tex");
    shader.alpha = glGetUniformLocation(prog, "alpha");
    shader.pos_attrib    = glGetAttribLocation(prog, "pos");
    shader.tex_attrib    = glGetAttribLocation(prog, "texcoord");
    shader.clip_rect     = glGetUniformLocation(prog, "clip_rect");
    shader.corner_radii  = glGetUniformLocation(prog, "corner_radii");
    shader.output_height = glGetUniformLocation(prog, "output_height");

    return shader;
}

#ifdef UNUSED
struct quad_external_shader make_quad_external_shader()
{
    GLuint prog = link_program(quad_vertex_src, tex_fragment_src_external);

    struct quad_external_shader shader = {};
    shader.prog = prog;
    shader.tex  = glGetUniformLocation(prog, "tex");
    shader.pos_attrib = glGetAttribLocation(prog, "pos");
    shader.tex_attrib = glGetAttribLocation(prog, "texcoord");

    return shader;
}

#endif
