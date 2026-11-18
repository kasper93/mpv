// Second round of pbo_upload_test.c. The first one showed on Apple's OpenGL
// 4.1 on Metal that glTexSubImage2D from a pixel unpack buffer with
// GL_UNPACK_ROW_LENGTH does not see what glBufferSubData wrote into the
// buffer right before. This one narrows that down and tries the ways
// libplacebo could fill and use the buffer instead. Nothing is shown on
// screen.
//
// macOS:   clang -O1 pbo_upload_test2.c -framework OpenGL -o pbo_upload_test2
// Windows: clang -O1 pbo_upload_test2.c -o pbo_upload_test2.exe -lopengl32 -lgdi32 -luser32
//
// Usage: pbo_upload_test2 [--delay=MS] [--frames=N] [--selftest] [--verbose]
//   --delay=MS   wait between frames (default 0, a display would be 16 to 42)
//   --selftest   leaves out one upload on purpose, the result has to be FAIL
// The exit code is the number of failed variants.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <dlfcn.h>
#include <unistd.h>
#define GLAPI_CALL
#else
#include <windows.h>
#include <GL/gl.h>
#define GLAPI_CALL APIENTRY
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
typedef char GLchar;
#define GL_ARRAY_BUFFER 0x8892
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#define GL_STREAM_DRAW 0x88E0
#define GL_STATIC_DRAW 0x88E4
#define GL_MAP_WRITE_BIT 0x0002
#define GL_R8 0x8229
#define GL_R16 0x822A
#define GL_RED 0x1903
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_TEXTURE0 0x84C0
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#define GL_FUNCS(X) \
    X(void, GenBuffers, (GLsizei, GLuint *)) \
    X(void, DeleteBuffers, (GLsizei, const GLuint *)) \
    X(void, BindBuffer, (GLenum, GLuint)) \
    X(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
    X(void, BufferSubData, (GLenum, GLintptr, GLsizeiptr, const void *))     X(void *, MapBufferRange, (GLenum, GLintptr, GLsizeiptr, GLbitfield))     X(GLboolean, UnmapBuffer, (GLenum)) \
    X(void, GenTextures, (GLsizei, GLuint *)) \
    X(void, DeleteTextures, (GLsizei, const GLuint *)) \
    X(void, BindTexture, (GLenum, GLuint)) \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, \
                         const void *)) \
    X(void, TexStorage2D, (GLenum, GLsizei, GLenum, GLsizei, GLsizei)) \
    X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, \
                            const void *)) \
    X(void, TexParameteri, (GLenum, GLenum, GLint)) \
    X(void, PixelStorei, (GLenum, GLint)) \
    X(void, GenFramebuffers, (GLsizei, GLuint *)) \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint *)) \
    X(void, BindFramebuffer, (GLenum, GLuint)) \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(GLenum, CheckFramebufferStatus, (GLenum)) \
    X(void, GenVertexArrays, (GLsizei, GLuint *)) \
    X(void, BindVertexArray, (GLuint)) \
    X(void, EnableVertexAttribArray, (GLuint)) \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(GLuint, CreateShader, (GLenum)) \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(void, CompileShader, (GLuint)) \
    X(void, GetShaderiv, (GLuint, GLenum, GLint *)) \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(GLuint, CreateProgram, (void)) \
    X(void, AttachShader, (GLuint, GLuint)) \
    X(void, BindAttribLocation, (GLuint, GLuint, const GLchar *)) \
    X(void, LinkProgram, (GLuint)) \
    X(void, GetProgramiv, (GLuint, GLenum, GLint *)) \
    X(void, UseProgram, (GLuint)) \
    X(GLint, GetUniformLocation, (GLuint, const GLchar *)) \
    X(void, Uniform1i, (GLint, GLint)) \
    X(void, ActiveTexture, (GLenum)) \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, DrawArrays, (GLenum, GLint, GLsizei)) \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, Clear, (GLbitfield)) \
    X(void, Flush, (void)) \
    X(void, Finish, (void)) \
    X(GLenum, GetError, (void)) \
    X(const GLubyte *, GetString, (GLenum))

static struct {
#define X(ret, name, args) ret (GLAPI_CALL *name) args;
    GL_FUNCS(X)
#undef X
} gl;

#ifdef __APPLE__

static bool choose_format(const CGLPixelFormatAttribute *attrs, CGLPixelFormatObj *pix)
{
    GLint npix = 0;
    CGLError err = CGLChoosePixelFormat(attrs, pix, &npix);
    if (err != kCGLNoError || !*pix) {
        printf("CGLChoosePixelFormat: %s\n", CGLErrorString(err));
        *pix = NULL;
        return false;
    }
    return true;
}

static bool init_context(void)
{
    // The profile cocoa-cb asks for first, it gives OpenGL 4.1
    CGLPixelFormatAttribute accelerated[] = {
        kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,
        kCGLPFAAccelerated, kCGLPFAAllowOfflineRenderers, (CGLPixelFormatAttribute)0,
    };
    CGLPixelFormatAttribute any[] = {
        kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,
        kCGLPFAAllowOfflineRenderers, (CGLPixelFormatAttribute)0,
    };
    CGLPixelFormatObj pix = NULL;
    if (!choose_format(accelerated, &pix)) {
        printf("no accelerated pixel format, trying any renderer\n");
        if (!choose_format(any, &pix))
            return false;
    }
    CGLContextObj ctx = NULL;
    CGLError err = CGLCreateContext(pix, NULL, &ctx);
    if (err != kCGLNoError || !ctx) {
        printf("CGLCreateContext: %s\n", CGLErrorString(err));
        return false;
    }
    CGLSetCurrentContext(ctx);
    // Looked up by name, so that nothing depends on which header declares
    // the functions newer than OpenGL 4.1
    bool ok = true;
#define X(ret, name, args) \
    gl.name = (ret (*) args)dlsym(RTLD_DEFAULT, "gl" #name); \
    if (!gl.name) { printf("no gl" #name "\n"); ok = false; }
    GL_FUNCS(X)
#undef X
    return ok;
}

static void sleep_ms(int ms)
{
    usleep(ms * 1000);
}

#else

static void *load(HMODULE lib, const char *name)
{
    void *p = (void *)wglGetProcAddress(name);
    if ((intptr_t)p >= -1 && (intptr_t)p <= 3)
        p = (void *)GetProcAddress(lib, name);
    if (!p)
        printf("no %s\n", name);
    return p;
}

// A window that is never shown provides the device context
static bool init_context(void)
{
    HMODULE lib = LoadLibraryW(L"opengl32.dll");
    WNDCLASSW wc = {
        .style = CS_OWNDC,
        .lpfnWndProc = DefWindowProcW,
        .hInstance = GetModuleHandleW(NULL),
        .lpszClassName = L"pbo_upload_test_hidden",
    };
    RegisterClassW(&wc);
    HWND wnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, wc.lpszClassName, L"",
                               WS_POPUP, 0, 0, 64, 64, NULL, NULL, wc.hInstance, NULL);
    if (!wnd)
        return false;
    HDC dc = GetDC(wnd);
    PIXELFORMATDESCRIPTOR pfd = {
        .nSize = sizeof(pfd), .nVersion = 1,
        .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        .iPixelType = PFD_TYPE_RGBA, .cColorBits = 32, .cAlphaBits = 8,
    };
    int pf = ChoosePixelFormat(dc, &pfd);
    if (!pf || !SetPixelFormat(dc, pf, &pfd))
        return false;
    HGLRC rc = wglCreateContext(dc);
    if (!rc || !wglMakeCurrent(dc, rc))
        return false;
    // WGL_CONTEXT_MAJOR_VERSION_ARB, MINOR, PROFILE_MASK = CORE_PROFILE_BIT
    typedef HGLRC (WINAPI *create_fn)(HDC, HGLRC, const int *);
    create_fn create = (create_fn)wglGetProcAddress("wglCreateContextAttribsARB");
    int attrs[] = {0x2091, 4, 0x2092, 1, 0x9126, 0x1, 0};
    HGLRC core = create ? create(dc, NULL, attrs) : NULL;
    if (!core || !wglMakeCurrent(dc, core))
        return false;
    wglDeleteContext(rc);
    bool ok = true;
#define X(ret, name, args) ok &= !!(gl.name = load(lib, "gl" #name));
    GL_FUNCS(X)
#undef X
    return ok;
}

static void sleep_ms(int ms)
{
    Sleep(ms);
}

#endif

enum {
    V_PBO           = 1 << 0,  // from a pixel unpack buffer, else from a pointer
    V_TIGHT         = 1 << 1,  // the row pitch is the width, no UNPACK_ROW_LENGTH
    V_ALIGN_PAD     = 1 << 2,  // rows padded to 8 bytes by UNPACK_ALIGNMENT, no UNPACK_ROW_LENGTH
    V_PAD_PLANES    = 1 << 3,  // the rows of the video planes are padded too
    V_DATA_AT_ALLOC = 1 << 4,  // glBufferData with the data and nothing else
    V_STATIC_DRAW   = 1 << 5,  // usage GL_STATIC_DRAW, else GL_STREAM_DRAW
    V_RESPEC_WRITE  = 1 << 6,  // glBufferData(NULL), then glBufferData with the data
    V_MAP_WRITE     = 1 << 7,  // glBufferData(NULL), then written through glMapBufferRange
    V_UNPACK_WRITE  = 1 << 8,  // glBufferSubData through the GL_PIXEL_UNPACK_BUFFER binding
    V_FLUSH_WRITE   = 1 << 9,  // glFlush between glBufferSubData and glTexSubImage2D
    V_FINISH_WRITE  = 1 << 10, // glFinish between glBufferSubData and glTexSubImage2D
    V_ROW_BY_ROW    = 1 << 11, // one glTexSubImage2D per row, no UNPACK_ROW_LENGTH
    V_ALLOC_ONCE    = 1 << 12, // one buffer allocated once, only glBufferSubData per upload
    V_PARAMS_AT_CREATE = 1 << 13, // only for the textures the test itself reads back
};

struct variant {
    const char *name;
    unsigned flags;
};

static const struct variant variants[] = {
    {"pointer", 0},
    {"libplacebo: glBufferData(NULL), glBufferSubData, ROW_LENGTH", V_PBO},
    {"  + tight rows, no UNPACK_ROW_LENGTH", V_PBO | V_TIGHT},
    {"  + rows padded by UNPACK_ALIGNMENT only", V_PBO | V_ALIGN_PAD},
    {"  + plane rows padded too", V_PBO | V_PAD_PLANES},
    {"  + one glTexSubImage2D per row, no UNPACK_ROW_LENGTH", V_PBO | V_ROW_BY_ROW},
    {"  + glBufferData with the data, STREAM_DRAW", V_PBO | V_DATA_AT_ALLOC},
    {"  + glBufferData with the data, STATIC_DRAW", V_PBO | V_DATA_AT_ALLOC | V_STATIC_DRAW},
    {"  + glBufferData with the data, plane rows padded",
     V_PBO | V_DATA_AT_ALLOC | V_PAD_PLANES},
    {"  + glBufferData(NULL), then glBufferData with the data", V_PBO | V_RESPEC_WRITE},
    {"  + written through glMapBufferRange", V_PBO | V_MAP_WRITE},
    {"  + glBufferSubData through GL_PIXEL_UNPACK_BUFFER", V_PBO | V_UNPACK_WRITE},
    {"  + glFlush after glBufferSubData", V_PBO | V_FLUSH_WRITE},
    {"  + glFinish after glBufferSubData", V_PBO | V_FINISH_WRITE},
    {"  + buffer allocated once, only glBufferSubData", V_PBO | V_ALLOC_ONCE},
};

#define MAXW 2048
#define MAXH 1024
#define MAX_FRAMES 64
#define NUM_ENTRIES 2

static int opt_delay, opt_frames = 24, opt_selftest, opt_verbose;

// Sizes of the atlas of the stats page and of a subtitle line over time, in
// the ranges the log of the report shows (1008x879 up to 2043x879, 269x130
// up to 1936x141).
static void entry_size(int e, int k, int *w, int *h)
{
    static const int sizes[NUM_ENTRIES][8][2] = {
        {{1008, 879}, {1001, 861}, {1008, 879}, {2015, 879}, {2040, 879}, {2033, 870},
         {2043, 879}, {2040, 892}},
        {{269, 130}, {1381, 141}, {900, 70}, {1381, 141}, {1936, 141}, {640, 141},
         {1936, 70}, {1100, 130}},
    };
    int i = (k * (e ? 3 : 1)) % 8;
    // The first rounds stay small, like the texture in the report did
    if (k < 3)
        i = e ? 0 : k % 3;
    *w = sizes[e][i][0];
    *h = sizes[e][i][1];
}

static uint8_t pattern(int e, int k, int x, int y)
{
    return (uint8_t)(17 + 91 * e + 37 * k + 3 * x + 7 * y + ((x ^ y) >> 2) + ((x * y) >> 5));
}

struct entry {
    GLuint tex;
    int tex_w, tex_h;
    GLuint result[MAX_FRAMES], fbo[MAX_FRAMES];
    int w[MAX_FRAMES], h[MAX_FRAMES];
    bool drawn[MAX_FRAMES];
    bool fresh[MAX_FRAMES]; // the upload of this frame was the first into a new texture
};

static GLuint program, vao, vbo, kept_buf, plane_tex[3], scratch_fbo, scratch_tex;
static GLuint plane_result[3][MAX_FRAMES], plane_fbo[3][MAX_FRAMES];
static uint8_t *host, *plane_host;

static bool check_err(const char *what)
{
    bool ok = true;
    GLenum err;
    while ((err = gl.GetError())) {
        printf("    GL error 0x%x in %s\n", err, what);
        ok = false;
    }
    return ok;
}

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = gl.CreateShader(type);
    gl.ShaderSource(s, 1, &src, NULL);
    gl.CompileShader(s);
    GLint ok = 0;
    gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        gl.GetShaderInfoLog(s, sizeof(log) - 1, NULL, log);
        printf("shader: %s\n", log);
    }
    return s;
}

static bool init_program(void)
{
    static const char vs[] =
        "#version 150\n"
        "in vec2 pos;\n"
        "void main() { gl_Position = vec4(pos, 0.0, 1.0); }\n";
    static const char fs[] =
        "#version 150\n"
        "uniform sampler2D tex;\n"
        "out vec4 color;\n"
        "void main() {\n"
        "    color = vec4(texelFetch(tex, ivec2(gl_FragCoord.xy), 0).r, 0.0, 0.0, 1.0);\n"
        "}\n";
    program = gl.CreateProgram();
    gl.AttachShader(program, compile(GL_VERTEX_SHADER, vs));
    gl.AttachShader(program, compile(GL_FRAGMENT_SHADER, fs));
    gl.BindAttribLocation(program, 0, "pos");
    gl.LinkProgram(program);
    GLint ok = 0;
    gl.GetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
        return false;
    gl.UseProgram(program);
    gl.Uniform1i(gl.GetUniformLocation(program, "tex"), 0);
    gl.GenVertexArrays(1, &vao);
    gl.BindVertexArray(vao);
    gl.GenBuffers(1, &vbo);
    gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    gl.BindBuffer(GL_ARRAY_BUFFER, 0);
    return check_err("init_program");
}

// libplacebo sets these before each draw and never at creation
static void set_tex_params(void)
{
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
}

static void create_tex(GLuint *tex, GLenum ifmt, GLenum type, int w, int h, unsigned flags)
{
    gl.GenTextures(1, tex);
    gl.BindTexture(GL_TEXTURE_2D, *tex);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl.TexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, GL_RED, type, NULL);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (flags & V_PARAMS_AT_CREATE)
        set_tex_params();
    gl.BindTexture(GL_TEXTURE_2D, 0);
}

// Large enough for every upload of the test
#define ONCE_SIZE ((size_t)(1920 + 64) * 1080 * 2)

// The transfer itself. Without further flags the calls and their order are
// those of libplacebo: gl_buf_create(), gl_buf_write(), gl_tex_upload(),
// gl_buf_destroy(). pitch is in bytes, texel is the size of a texel in bytes.
static void upload(GLuint tex, GLenum type, int texel, int w, int h, int pitch,
                   const void *data, unsigned flags)
{
    size_t size = (size_t)(h - 1) * pitch + (size_t)w * texel;
    GLenum usage = flags & V_STATIC_DRAW ? GL_STATIC_DRAW : GL_STREAM_DRAW;
    GLuint buf = 0;
    uintptr_t src = (uintptr_t)data;
    if (flags & V_PBO) {
        if (flags & V_ALLOC_ONCE) {
            if (!kept_buf) {
                gl.GenBuffers(1, &kept_buf);
                gl.BindBuffer(GL_ARRAY_BUFFER, kept_buf);
                gl.BufferData(GL_ARRAY_BUFFER, ONCE_SIZE, NULL, usage);
                gl.BindBuffer(GL_ARRAY_BUFFER, 0);
            }
            buf = kept_buf;
        } else {
            gl.GenBuffers(1, &buf);
            gl.BindBuffer(GL_ARRAY_BUFFER, buf);
            gl.BufferData(GL_ARRAY_BUFFER, size, flags & V_DATA_AT_ALLOC ? data : NULL, usage);
            gl.BindBuffer(GL_ARRAY_BUFFER, 0);
        }
        if (!(flags & V_DATA_AT_ALLOC)) {
            GLenum target = flags & V_UNPACK_WRITE ? GL_PIXEL_UNPACK_BUFFER : GL_ARRAY_BUFFER;
            gl.BindBuffer(target, buf);
            if (flags & V_RESPEC_WRITE) {
                gl.BufferData(target, size, data, usage);
            } else if (flags & V_MAP_WRITE) {
                void *map = gl.MapBufferRange(target, 0, size, GL_MAP_WRITE_BIT);
                if (map)
                    memcpy(map, data, size);
                gl.UnmapBuffer(target);
            } else {
                gl.BufferSubData(target, 0, size, data);
            }
            gl.BindBuffer(target, 0);
            if (flags & V_FLUSH_WRITE)
                gl.Flush();
            if (flags & V_FINISH_WRITE)
                gl.Finish();
        }
        gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, buf);
        src = 0;
    }
    bool padded = pitch != w * texel;
    gl.PixelStorei(GL_UNPACK_ALIGNMENT,
                   pitch % 8 == 0 ? 8 : pitch % 4 == 0 ? 4 : pitch % 2 == 0 ? 2 : 1);
    gl.BindTexture(GL_TEXTURE_2D, tex);
    if (padded && (flags & V_ROW_BY_ROW)) {
        for (int y = 0; y < h; y++) {
            gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, y, w, 1, GL_RED, type,
                             (void *)(src + (size_t)y * pitch));
        }
    } else {
        if (padded && !(flags & V_ALIGN_PAD))
            gl.PixelStorei(GL_UNPACK_ROW_LENGTH, pitch / texel);
        gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, type, (void *)src);
    }
    gl.BindTexture(GL_TEXTURE_2D, 0);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    if (flags & V_PBO) {
        gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        if (!(flags & V_ALLOC_ONCE))
            gl.DeleteBuffers(1, &buf);
    }
}

// Copies the lower left w x h texels of tex into the framebuffer
static void draw_tex(GLuint tex, GLuint fbo, int w, int h)
{
    static const float verts[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl.Viewport(0, 0, w, h);
    gl.UseProgram(program);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, tex);
    set_tex_params();
    gl.BindVertexArray(vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
    gl.BufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl.BindBuffer(GL_ARRAY_BUFFER, 0);
    gl.BindTexture(GL_TEXTURE_2D, 0);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void upload_entry(struct entry *en, int e, int k, unsigned flags)
{
    int w, h;
    entry_size(e, k, &w, &h);
    en->w[k] = w;
    en->h[k] = h;

    // Like mpv: the texture only grows, and a larger one replaces it
    if (!en->tex || en->tex_w < w || en->tex_h < h) {
        int tw = w > en->tex_w ? w : en->tex_w, th = h > en->tex_h ? h : en->tex_h;
        if (en->tex)
            gl.DeleteTextures(1, &en->tex);
        create_tex(&en->tex, GL_R8, GL_UNSIGNED_BYTE, tw, th, 0);
        en->tex_w = tw;
        en->tex_h = th;
        en->fresh[k] = true;
    }

    // The image mpv packs into has a power of two width. The padding of the
    // rows holds something else than the picture, as the real one does.
    int pitch = w <= 1024 ? 1024 : 2048;
    if (flags & V_TIGHT)
        pitch = w;
    if (flags & V_ALIGN_PAD)
        pitch = (w + 7) & ~7;
    for (int y = 0; y < h; y++) {
        uint8_t *row = host + (size_t)y * pitch;
        for (int x = 0; x < w; x++)
            row[x] = pattern(e, k, x, y);
        for (int x = w; x < pitch; x++)
            row[x] = (uint8_t)(0xA5 ^ k);
    }
    if (opt_selftest && e == 0 && k == 5)
        return;
    upload(en->tex, GL_UNSIGNED_BYTE, 1, w, h, pitch, host, flags);
}

static void draw_entry(struct entry *en, int k)
{
    draw_tex(en->tex, en->fbo[k], en->w[k], en->h[k]);
    en->drawn[k] = true;
}

// Returns the number of wrong texels of what the draw call of frame k saw.
static long verify(struct entry *en, int e, int k, uint8_t *got, int frames, char *note,
                   size_t note_size)
{
    int w = en->w[k], h = en->h[k];
    gl.BindFramebuffer(GL_FRAMEBUFFER, en->fbo[k]);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl.ReadPixels(0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, got);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    long wrong = 0;
    int fx = -1, fy = -1;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (got[(size_t)y * w + x] != pattern(e, k, x, y)) {
                if (!wrong)
                    fx = x, fy = y;
                wrong++;
            }
        }
    }
    note[0] = 0;
    if (!wrong)
        return 0;
    // Is it the picture of another frame?
    int best = -1;
    long best_same = 0;
    for (int j = 0; j < frames; j++) {
        if (j == k)
            continue;
        long same = 0;
        for (int y = 0; y < h; y += 3) {
            for (int x = 0; x < w; x += 3)
                same += got[(size_t)y * w + x] == pattern(e, j, x, y);
        }
        if (same > best_same)
            best_same = same, best = j;
    }
    long samples = (long)((h + 2) / 3) * ((w + 2) / 3);
    long zero = 0, pad = 0;
    for (size_t i = 0; i < (size_t)w * h; i++) {
        zero += got[i] == 0;
        pad += got[i] == (uint8_t)(0xA5 ^ k);
    }
    snprintf(note, note_size,
             "%ld of %ld texels wrong, first at %d,%d (got %d, want %d), %ld%% equal to "
             "frame %d, %ld%% zero, %ld%% row padding",
             wrong, (long)w * h, fx, fy, got[(size_t)fy * w + fx], pattern(e, k, fx, fy),
             best >= 0 ? best_same * 100 / samples : 0, best, zero * 100 / ((long)w * h),
             pad * 100 / ((long)w * h));
    return wrong;
}

static GLuint make_result(int w, int h, GLuint *fbo)
{
    GLuint tex;
    create_tex(&tex, GL_R8, GL_UNSIGNED_BYTE, w, h, V_PARAMS_AT_CREATE);
    gl.GenFramebuffers(1, fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, *fbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        printf("    framebuffer incomplete\n");
    gl.ClearColor(0, 0, 0, 0);
    gl.Clear(GL_COLOR_BUFFER_BIT);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    return tex;
}

// The planes are checked in their lower left corner only
#define PLANE_CHECK 256

static bool run_variant(const struct variant *v)
{
    static struct entry entries[NUM_ENTRIES];
    memset(entries, 0, sizeof(entries));
    unsigned flags = v->flags;
    int frames = opt_frames;
    bool planes = true;

    for (int e = 0; e < NUM_ENTRIES; e++) {
        for (int k = 0; k < frames; k++)
            entries[e].result[k] = make_result(MAXW, MAXH, &entries[e].fbo[k]);
    }
    for (int i = 0; i < 3; i++) {
        for (int k = 0; k < frames; k++)
            plane_result[i][k] = make_result(PLANE_CHECK, PLANE_CHECK, &plane_fbo[i][k]);
    }
    scratch_tex = make_result(16, 16, &scratch_fbo);
    static const int plane_w[3] = {1920, 960, 960}, plane_h[3] = {1080, 540, 540};
    // A video frame gets new textures now and then (after a seek, when the
    // queue grows), here every eighth frame.
    memset(plane_tex, 0, sizeof(plane_tex));
    gl.Finish();

    for (int k = 0; k < frames; k++) {
        // The order of a frame in the trace: OSD atlas, subtitle atlas when
        // it changed, the three planes of a new video frame, then the draws.
        upload_entry(&entries[0], 0, k, flags);
        bool sub_changed = k % 2 == 0;
        if (sub_changed)
            upload_entry(&entries[1], 1, k, flags);
        for (int i = 0; i < 3 && planes; i++) {
            if (k % 8 == 0) {
                if (plane_tex[i])
                    gl.DeleteTextures(1, &plane_tex[i]);
                create_tex(&plane_tex[i], GL_R16, GL_UNSIGNED_SHORT, plane_w[i], plane_h[i], 0);
            }
            // Tight rows like a 1920 wide video has, or padded ones like most
            // other widths get from the decoder
            int pitch = (plane_w[i] + (flags & V_PAD_PLANES ? 32 : 0)) * 2;
            memset(plane_host, 0x40 + k + i, (size_t)pitch * plane_h[i]);
            upload(plane_tex[i], GL_UNSIGNED_SHORT, 2, plane_w[i], plane_h[i], pitch,
                   plane_host, flags & ~V_ALIGN_PAD);
        }
        for (int i = 0; i < 3 && planes; i++)
            draw_tex(plane_tex[i], plane_fbo[i][k], PLANE_CHECK, PLANE_CHECK);
        draw_entry(&entries[0], k);
        if (sub_changed)
            draw_entry(&entries[1], k);
        gl.Flush();
        if (opt_delay)
            sleep_ms(opt_delay);
    }
    bool ok = check_err(v->name);
    gl.Finish();

    uint8_t *got = malloc((size_t)MAXW * MAXH);
    int bad = 0, bad_fresh = 0, total = 0, total_fresh = 0, shown = 0;
    char note[512];
    for (int e = 0; e < NUM_ENTRIES; e++) {
        for (int k = 0; k < frames; k++) {
            if (!entries[e].drawn[k])
                continue;
            total++;
            total_fresh += entries[e].fresh[k];
            if (verify(&entries[e], e, k, got, frames, note, sizeof(note))) {
                if (shown++ < 6 || opt_verbose) {
                    printf("    %s frame %d (%dx%d%s): %s\n", e ? "subtitle" : "OSD", k,
                           entries[e].w[k], entries[e].h[k],
                           entries[e].fresh[k] ? ", first upload into a new texture" : "", note);
                }
                bad++;
                bad_fresh += entries[e].fresh[k];
            }
        }
    }
    int bad_planes = 0, total_planes = 0;
    for (int i = 0; i < 3 && planes; i++) {
        for (int k = 0; k < frames; k++) {
            gl.BindFramebuffer(GL_FRAMEBUFFER, plane_fbo[i][k]);
            gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
            gl.ReadPixels(0, 0, PLANE_CHECK, PLANE_CHECK, GL_RED, GL_UNSIGNED_BYTE, got);
            long wrong = 0;
            for (int n = 0; n < PLANE_CHECK * PLANE_CHECK; n++)
                wrong += got[n] != (uint8_t)(0x40 + k + i);
            total_planes++;
            if (wrong) {
                if (shown++ < 6 || opt_verbose) {
                    printf("    plane %d frame %d%s: %ld of %d checked texels wrong (got %d, "
                           "want %d)\n", i, k, k % 8 == 0 ? ", first upload into a new texture"
                           : "", wrong, PLANE_CHECK * PLANE_CHECK, got[0], 0x40 + k + i);
                }
                bad_planes++;
            }
        }
    }
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    free(got);
    ok &= check_err("verify");
    bool pass = !bad && !bad_planes && ok;
    printf("%s  %-60s wrong: %d of %d atlas draws (%d of %d after a new texture), "
           "%d of %d plane draws\n", pass ? "PASS" : "FAIL", v->name, bad, total, bad_fresh,
           total_fresh, bad_planes, total_planes);

    for (int e = 0; e < NUM_ENTRIES; e++) {
        gl.DeleteTextures(1, &entries[e].tex);
        gl.DeleteTextures(frames, entries[e].result);
        gl.DeleteFramebuffers(frames, entries[e].fbo);
    }
    for (int i = 0; i < 3; i++) {
        gl.DeleteTextures(frames, plane_result[i]);
        gl.DeleteFramebuffers(frames, plane_fbo[i]);
    }
    gl.DeleteTextures(3, plane_tex);
    gl.DeleteTextures(1, &scratch_tex);
    gl.DeleteFramebuffers(1, &scratch_fbo);
    if (kept_buf)
        gl.DeleteBuffers(1, &kept_buf);
    kept_buf = 0;
    gl.Finish();
    return pass;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "--delay=", 8)) {
            opt_delay = atoi(argv[i] + 8);
        } else if (!strncmp(argv[i], "--frames=", 9)) {
            opt_frames = atoi(argv[i] + 9);
        } else if (!strcmp(argv[i], "--selftest")) {
            opt_selftest = 1;
        } else if (!strcmp(argv[i], "--verbose")) {
            opt_verbose = 1;
        }
    }
    if (opt_frames < 1 || opt_frames > MAX_FRAMES)
        opt_frames = 24;
    if (!init_context()) {
        printf("no OpenGL context\n");
        return 100;
    }
    printf("GL_VERSION:  %s\nGL_VENDOR:   %s\nGL_RENDERER: %s\n", gl.GetString(GL_VERSION),
           gl.GetString(GL_VENDOR), gl.GetString(GL_RENDERER));
    printf("%d frames per variant, %d ms between frames%s\n", opt_frames, opt_delay,
           opt_selftest ? ", selftest (one upload left out)" : "");
    if (!init_program()) {
        printf("no shader program\n");
        return 100;
    }
    host = malloc((size_t)MAXW * MAXH);
    plane_host = malloc(ONCE_SIZE);

    int failed = 0;
    for (size_t i = 0; i < sizeof(variants) / sizeof(variants[0]); i++)
        failed += !run_variant(&variants[i]);
    printf("%d of %d variants failed\n", failed, (int)(sizeof(variants) / sizeof(variants[0])));
    return failed;
}
