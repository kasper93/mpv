// The OSD atlas uploads of mpv's gpu-next renderer through libplacebo's own
// API on an OpenGL context: pl_tex_recreate() of a texture that only grows,
// pl_tex_upload() of a part of it with row padding, with and without the
// callback that selects libplacebo's pixel buffer path, then a draw that
// samples the texture. What each draw saw is read back at the end.
// Nothing is shown on screen.
//
// macOS:   clang -O1 pl_osd_upload_test.c $(pkg-config --cflags --libs libplacebo) \
//              -framework OpenGL -o pl_osd_upload_test
// Windows: clang -O1 pl_osd_upload_test.c -o pl_osd_upload_test.exe \
//              $(pkg-config --cflags --libs libplacebo) -lopengl32 -lgdi32 -luser32
//
// Usage: pl_osd_upload_test [--delay=MS] [--frames=N] [--selftest] [--verbose]
// The exit code is the number of failed variants.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libplacebo/dispatch.h>
#include <libplacebo/gpu.h>
#include <libplacebo/log.h>
#include <libplacebo/opengl.h>
#include <libplacebo/shaders/sampling.h>

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <dlfcn.h>
#include <unistd.h>

static pl_voidfunc_t get_proc(const char *name)
{
    return (pl_voidfunc_t)dlsym(RTLD_DEFAULT, name);
}

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
    return true;
}

static void sleep_ms(int ms)
{
    usleep(ms * 1000);
}

#else

#include <windows.h>

static HMODULE opengl32;

static pl_voidfunc_t get_proc(const char *name)
{
    void *p = (void *)wglGetProcAddress(name);
    if ((intptr_t)p >= -1 && (intptr_t)p <= 3)
        p = (void *)GetProcAddress(opengl32, name);
    return (pl_voidfunc_t)p;
}

// A window that is never shown provides the device context
static bool init_context(void)
{
    opengl32 = LoadLibraryW(L"opengl32.dll");
    WNDCLASSW wc = {
        .style = CS_OWNDC,
        .lpfnWndProc = DefWindowProcW,
        .hInstance = GetModuleHandleW(NULL),
        .lpszClassName = L"pl_osd_upload_test_hidden",
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
    return true;
}

static void sleep_ms(int ms)
{
    Sleep(ms);
}

#endif

enum {
    V_CALLBACK  = 1 << 0, // pl_tex_upload() with a callback, as mpv does when the GPU has them
    V_NO_PLANES = 1 << 1, // without the three video plane uploads in between
    V_TIGHT     = 1 << 2, // the row pitch is the width
    V_WHOLE_TEX = 1 << 3, // the texture is recreated to the exact size of every upload
    V_NO_FLUSH  = 1 << 4, // no pl_gpu_flush() at the end of a frame
};

struct variant {
    const char *name;
    unsigned flags;
};

static const struct variant variants[] = {
    {"no callback (upload from the pointer)", 0},
    {"callback, as mpv (pixel buffer path)", V_CALLBACK},
    {"  same without a flush per frame", V_CALLBACK | V_NO_FLUSH},
    {"  same without the plane uploads", V_CALLBACK | V_NO_PLANES},
    {"  + tight rows", V_CALLBACK | V_TIGHT},
    {"  + upload always covers the whole texture", V_CALLBACK | V_WHOLE_TEX},
    {"  + tight rows and whole texture", V_CALLBACK | V_TIGHT | V_WHOLE_TEX},
};

#define MAXW 2048
#define MAXH 1024
#define MAX_FRAMES 64
#define NUM_ENTRIES 2
#define PLANE_CHECK 256

static int opt_delay, opt_frames = 24, opt_selftest, opt_verbose;
static int callbacks_pending;

// Same sizes over time as pbo_upload_test.c
static void entry_size(int e, int k, int *w, int *h)
{
    static const int sizes[NUM_ENTRIES][8][2] = {
        {{1008, 879}, {1001, 861}, {1008, 879}, {2015, 879}, {2040, 879}, {2033, 870},
         {2043, 879}, {2040, 892}},
        {{269, 130}, {1381, 141}, {900, 70}, {1381, 141}, {1936, 141}, {640, 141},
         {1936, 70}, {1100, 130}},
    };
    int i = (k * (e ? 3 : 1)) % 8;
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
    pl_tex tex;
    pl_tex result[MAX_FRAMES];
    int w[MAX_FRAMES], h[MAX_FRAMES];
    bool drawn[MAX_FRAMES];
    bool fresh[MAX_FRAMES]; // the upload of this frame was the first into a new texture
};

static pl_gpu gpu;
static pl_dispatch dp;
static pl_fmt r8, r16;

// mpv frees its reference to the image here
static void upload_done(void *priv)
{
    free(priv);
    callbacks_pending--;
}

static bool upload(pl_tex tex, int texel, int w, int h, int pitch, const uint8_t *data,
                   unsigned flags)
{
    struct pl_tex_transfer_params params = {
        .tex = tex,
        .rc = {.x1 = w, .y1 = h},
        .row_pitch = pitch,
        .ptr = (void *)data,
    };
    if (flags & V_CALLBACK) {
        // The image has to stay valid until the callback, like the
        // reference mpv holds
        size_t size = (size_t)(h - 1) * pitch + (size_t)w * texel;
        void *copy = malloc(size);
        memcpy(copy, data, size);
        params.ptr = copy;
        params.callback = upload_done;
        params.priv = copy;
        callbacks_pending++;
    }
    return pl_tex_upload(gpu, &params);
}

static bool upload_entry(struct entry *en, int e, int k, unsigned flags, uint8_t *host)
{
    int w, h;
    entry_size(e, k, &w, &h);
    en->w[k] = w;
    en->h[k] = h;

    // The logic of upload_overlay_tex() in mpv
    pl_tex tex = en->tex;
    bool recreate = !tex || tex->params.w < w || tex->params.h < h;
    if (flags & V_WHOLE_TEX)
        recreate = !tex || tex->params.w != w || tex->params.h != h;
    if (recreate) {
        int tw = tex && tex->params.w > w ? tex->params.w : w;
        int th = tex && tex->params.h > h ? tex->params.h : h;
        if (flags & V_WHOLE_TEX)
            tw = w, th = h;
        bool ok = pl_tex_recreate(gpu, &en->tex, &(struct pl_tex_params) {
            .format = r8,
            .w = tw,
            .h = th,
            .host_writable = true,
            .sampleable = true,
        });
        if (!ok)
            return false;
        en->fresh[k] = true;
    }

    int pitch = w <= 1024 ? 1024 : 2048;
    if (flags & V_TIGHT)
        pitch = w;
    for (int y = 0; y < h; y++) {
        uint8_t *row = host + (size_t)y * pitch;
        for (int x = 0; x < w; x++)
            row[x] = pattern(e, k, x, y);
        for (int x = w; x < pitch; x++)
            row[x] = (uint8_t)(0xA5 ^ k);
    }
    if (opt_selftest && e == 0 && k == 5)
        return true;
    return upload(en->tex, 1, w, h, pitch, host, flags);
}

// Copies the lower left w x h texels of tex into the target
static bool draw_tex(pl_tex tex, pl_tex target, int w, int h)
{
    pl_shader sh = pl_dispatch_begin(dp);
    bool ok = pl_shader_sample_nearest(sh, pl_sample_src(
        .tex = tex,
        .rect = {0, 0, w, h},
        .new_w = w,
        .new_h = h,
    ));
    if (!ok) {
        pl_dispatch_abort(dp, &sh);
        return false;
    }
    return pl_dispatch_finish(dp, pl_dispatch_params(
        .shader = &sh,
        .target = target,
        .rect = {0, 0, w, h},
    ));
}

static pl_tex make_result(int w, int h)
{
    return pl_tex_create(gpu, pl_tex_params(
        .format = r8,
        .w = w,
        .h = h,
        .renderable = true,
        .host_readable = true,
    ));
}

static long verify(struct entry *en, int e, int k, uint8_t *got, int frames, char *note,
                   size_t note_size)
{
    int w = en->w[k], h = en->h[k];
    note[0] = 0;
    bool ok = pl_tex_download(gpu, pl_tex_transfer_params(
        .tex = en->result[k],
        .rc = {.x1 = w, .y1 = h},
        .row_pitch = w,
        .ptr = got,
    ));
    if (!ok) {
        snprintf(note, note_size, "download failed");
        return -1;
    }
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
    if (!wrong)
        return 0;
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

static bool run_variant(const struct variant *v, uint8_t *host, uint8_t *plane_host)
{
    static struct entry entries[NUM_ENTRIES];
    static pl_tex plane_result[3][MAX_FRAMES];
    memset(entries, 0, sizeof(entries));
    memset(plane_result, 0, sizeof(plane_result));
    unsigned flags = v->flags;
    int frames = opt_frames;
    bool planes = !(flags & V_NO_PLANES);
    bool ok = true;

    for (int e = 0; e < NUM_ENTRIES; e++) {
        for (int k = 0; k < frames; k++)
            ok &= !!(entries[e].result[k] = make_result(MAXW, MAXH));
    }
    for (int i = 0; i < 3; i++) {
        for (int k = 0; k < frames; k++)
            ok &= !!(plane_result[i][k] = make_result(PLANE_CHECK, PLANE_CHECK));
    }
    static const int plane_w[3] = {1920, 960, 960}, plane_h[3] = {1080, 540, 540};
    pl_tex plane_tex[3] = {0};
    pl_gpu_finish(gpu);

    for (int k = 0; k < frames && ok; k++) {
        pl_dispatch_reset_frame(dp);
        ok &= upload_entry(&entries[0], 0, k, flags, host);
        bool sub_changed = k % 2 == 0;
        if (sub_changed)
            ok &= upload_entry(&entries[1], 1, k, flags, host);
        for (int i = 0; i < 3 && planes; i++) {
            // A video frame gets new textures now and then, here every eighth frame
            if (k % 8 == 0)
                pl_tex_destroy(gpu, &plane_tex[i]);
            ok &= pl_tex_recreate(gpu, &plane_tex[i], pl_tex_params(
                .format = r16,
                .w = plane_w[i],
                .h = plane_h[i],
                .host_writable = true,
                .sampleable = true,
            ));
            memset(plane_host, 0x40 + k + i, (size_t)plane_w[i] * plane_h[i] * 2);
            ok &= upload(plane_tex[i], 2, plane_w[i], plane_h[i], plane_w[i] * 2, plane_host,
                         flags);
        }
        for (int i = 0; i < 3 && planes; i++)
            ok &= draw_tex(plane_tex[i], plane_result[i][k], PLANE_CHECK, PLANE_CHECK);
        ok &= draw_tex(entries[0].tex, entries[0].result[k], entries[0].w[k], entries[0].h[k]);
        entries[0].drawn[k] = true;
        if (sub_changed) {
            ok &= draw_tex(entries[1].tex, entries[1].result[k], entries[1].w[k],
                           entries[1].h[k]);
            entries[1].drawn[k] = true;
        }
        if (!(flags & V_NO_FLUSH))
            pl_gpu_flush(gpu);
        if (opt_delay)
            sleep_ms(opt_delay);
    }
    if (!ok)
        printf("    a libplacebo call failed\n");
    pl_gpu_finish(gpu);

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
            if (!plane_result[i][k])
                continue;
            bool dl = pl_tex_download(gpu, pl_tex_transfer_params(
                .tex = plane_result[i][k],
                .row_pitch = PLANE_CHECK,
                .ptr = got,
            ));
            long wrong = !dl;
            for (int n = 0; n < PLANE_CHECK * PLANE_CHECK && dl; n++)
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
    free(got);
    bool pass = !bad && !bad_planes && ok;
    printf("%s  %-44s wrong: %d of %d atlas draws (%d of %d after a new texture), "
           "%d of %d plane draws\n", pass ? "PASS" : "FAIL", v->name, bad, total, bad_fresh,
           total_fresh, bad_planes, total_planes);

    for (int e = 0; e < NUM_ENTRIES; e++) {
        pl_tex_destroy(gpu, &entries[e].tex);
        for (int k = 0; k < frames; k++)
            pl_tex_destroy(gpu, &entries[e].result[k]);
    }
    for (int i = 0; i < 3; i++) {
        pl_tex_destroy(gpu, &plane_tex[i]);
        for (int k = 0; k < frames; k++)
            pl_tex_destroy(gpu, &plane_result[i][k]);
    }
    pl_gpu_finish(gpu);
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
    pl_log log = pl_log_create(PL_API_VER, pl_log_params(
        .log_cb = pl_log_simple,
        .log_level = opt_verbose ? PL_LOG_DEBUG : PL_LOG_INFO,
    ));
    pl_opengl gl = pl_opengl_create(log, pl_opengl_params(
        .get_proc_addr = get_proc,
        .allow_software = true,
    ));
    if (!gl) {
        printf("pl_opengl_create failed\n");
        return 100;
    }
    gpu = gl->gpu;
    r8 = pl_find_named_fmt(gpu, "r8");
    r16 = pl_find_named_fmt(gpu, "r16");
    dp = pl_dispatch_create(log, gpu);
    if (!r8 || !r16 || !dp) {
        printf("no r8 or r16 format\n");
        return 100;
    }
    printf("libplacebo %s, callbacks %d, %d frames per variant, %d ms between frames%s\n",
           pl_version(), (int)gpu->limits.callbacks, opt_frames, opt_delay,
           opt_selftest ? ", selftest (one upload left out)" : "");
    uint8_t *host = malloc((size_t)MAXW * MAXH);
    uint8_t *plane_host = malloc((size_t)1920 * 1080 * 2);

    int failed = 0;
    for (size_t i = 0; i < sizeof(variants) / sizeof(variants[0]); i++)
        failed += !run_variant(&variants[i], host, plane_host);
    printf("%d of %d variants failed\n", failed, (int)(sizeof(variants) / sizeof(variants[0])));
    pl_gpu_finish(gpu);
    if (callbacks_pending)
        printf("%d upload callbacks never ran\n", callbacks_pending);
    pl_dispatch_destroy(&dp);
    pl_opengl_destroy(&gl);
    pl_log_destroy(&log);
    return failed;
}
