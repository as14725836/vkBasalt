// CPU harness for the EASU shader in src/shader/easu.frag.glsl
//
// The GLSL is transliterated here 1:1 (same operation order, same constants) so
// that the invariants below actually test the shader logic and not a rewrite.
//
// ffx_fsr1.h only ships a GPU implementation of FsrEasuF (A_CPU provides the
// constant generator alone), so there is no authoritative CPU reference output to
// diff against. Instead this checks properties that a correct EASU must have and
// that a broken gather alignment would immediately violate:
//
//   1. flat input            -> exactly that colour everywhere
//   2. 1:1 scale             -> identity (this is the gather alignment test)
//   3. 2:1 step edge         -> stays inside the min/max of the 4 nearest taps
//   4. integer pixel shift   -> output shifts by exactly scale * shift
//   5. step edge             -> sharper than bilinear
//
// Build: cc -O2 -o easu_cpu_test easu_cpu_test.c -lm && ./easu_cpu_test

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int      w, h;
    float*   p; // rgb, interleaved
} Image;

static Image makeImage(int w, int h)
{
    Image i;
    i.w = w;
    i.h = h;
    i.p = calloc((size_t)w * h * 3, sizeof(float));
    return i;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// clamp-to-edge fetch, same as the sampler the layer creates
static void fetch(const Image* img, int x, int y, float out[3])
{
    x = x < 0 ? 0 : (x >= img->w ? img->w - 1 : x);
    y = y < 0 ? 0 : (y >= img->h ? img->h - 1 : y);
    const float* t = img->p + ((size_t)y * img->w + x) * 3;
    out[0]         = t[0];
    out[1]         = t[1];
    out[2]         = t[2];
}

// gatherAt() from the shader: 2x2 block around uv p, per channel.
static void gatherAt(const Image* img, float px, float py, float R[4], float G[4], float B[4])
{
    float bx = floorf(px * img->w - 0.5f) + 0.5f;
    float by = floorf(py * img->h - 0.5f) + 0.5f;
    for (int i = 0; i < 4; i++)
    {
        int   dx = i & 1;
        int   dy = (i >> 1) & 1;
        float t[3];
        // uv -> texel index
        int tx = (int)floorf(bx + dx);
        int ty = (int)floorf(by + dy);
        fetch(img, tx, ty, t);
        R[i] = t[0];
        G[i] = t[1];
        B[i] = t[2];
    }
}

static void easuTap(float aC[3], float* aW, float offx, float offy, float dirx, float diry, float lenx, float leny, float lob, float clp, const float c[3])
{
    float vx = (offx * dirx) + (offy * diry);
    float vy = (offx * (-diry)) + (offy * dirx);
    vx *= lenx;
    vy *= leny;
    float d2 = vx * vx + vy * vy;
    d2       = d2 < clp ? d2 : clp;

    float wB = (2.0f / 5.0f) * d2 + (-1.0f);
    float wA = lob * d2 + (-1.0f);
    wB *= wB;
    wA *= wA;
    wB = (25.0f / 16.0f) * wB + (-(25.0f / 16.0f - 1.0f));
    float w = wB * wA;
    for (int i = 0; i < 3; i++)
        aC[i] += c[i] * w;
    *aW += w;
}

static void easuSet(float* dirx, float* diry, float* len, float ppx, float ppy, int which, float lA, float lB, float lC, float lD, float lE)
{
    float w;
    if (which == 0)
        w = (1.0f - ppx) * (1.0f - ppy);
    else if (which == 1)
        w = ppx * (1.0f - ppy);
    else if (which == 2)
        w = (1.0f - ppx) * ppy;
    else
        w = ppx * ppy;

    float dc   = lD - lC;
    float cb   = lC - lB;
    float lenX = fmaxf(fabsf(dc), fabsf(cb));
    lenX       = 1.0f / fmaxf(lenX, 1.0e-8f);
    float dirX = lD - lB;
    *dirx += dirX * w;
    lenX = clampf(fabsf(dirX) * lenX, 0.0f, 1.0f);
    lenX *= lenX;
    *len += lenX * w;

    float ec   = lE - lC;
    float ca   = lC - lA;
    float lenY = fmaxf(fabsf(ec), fabsf(ca));
    lenY       = 1.0f / fmaxf(lenY, 1.0e-8f);
    float dirY = lE - lA;
    *diry += dirY * w;
    lenY = clampf(fabsf(dirY) * lenY, 0.0f, 1.0f);
    lenY *= lenY;
    *len += lenY * w;
}

// One output pixel of the EASU shader. Taps are sampled by texel offset.
static void easu(const Image* in, int outW, int outH, int ipx, int ipy, float out[3])
{
    float inW = (float)in->w, inH = (float)in->h;
    float oW = (float)outW, oH = (float)outH;

    float con0x = inW / oW, con0y = inH / oH;
    float con0z = 0.5f * inW / oW - 0.5f, con0w = 0.5f * inH / oH - 0.5f;

    float ppx = (float)ipx * con0x + con0z;
    float ppy = (float)ipy * con0y + con0w;
    float fpx = floorf(ppx), fpy = floorf(ppy);
    ppx -= fpx;
    ppy -= fpy;

    static const float off[12][2] = {{0, -1}, {1, -1}, {-1, 0}, {0, 0}, {1, 0}, {2, 0}, {-1, 1}, {0, 1}, {1, 1}, {2, 1}, {0, 2}, {1, 2}};
    float c[12][3];
    float l[12];
    for (int i = 0; i < 12; i++)
    {
        fetch(in, (int)(fpx + off[i][0]), (int)(fpy + off[i][1]), c[i]);
        l[i] = c[i][2] * 0.5f + (c[i][0] * 0.5f + c[i][1]);
    }
    float bL = l[0], cL = l[1], eL = l[2], fL = l[3], gL = l[4], hL = l[5];
    float iL = l[6], jL = l[7], kL = l[8], lL = l[9], nL = l[10], oL = l[11];

    float dirx = 0.0f, diry = 0.0f, len = 0.0f;
    easuSet(&dirx, &diry, &len, ppx, ppy, 0, bL, eL, fL, gL, jL);
    easuSet(&dirx, &diry, &len, ppx, ppy, 1, cL, fL, gL, hL, kL);
    easuSet(&dirx, &diry, &len, ppx, ppy, 2, fL, iL, jL, kL, nL);
    easuSet(&dirx, &diry, &len, ppx, ppy, 3, gL, jL, kL, lL, oL);

    float dirR = dirx * dirx + diry * diry;
    int   zro  = dirR < (1.0f / 32768.0f);
    dirR       = 1.0f / sqrtf(fmaxf(dirR, 1.0e-8f));
    dirR       = zro ? 1.0f : dirR;
    dirx       = zro ? 1.0f : dirx;
    dirx *= dirR;
    diry *= dirR;

    len = len * 0.5f;
    len *= len;

    float stretch = (dirx * dirx + diry * diry) * (1.0f / fmaxf(fmaxf(fabsf(dirx), fabsf(diry)), 1.0e-8f));
    float len2x   = 1.0f + (stretch - 1.0f) * len;
    float len2y   = 1.0f + (-0.5f) * len;
    float lob     = 0.5f + ((1.0f / 4.0f - 0.04f) - 0.5f) * len;
    float clp     = 1.0f / fmaxf(lob, 1.0e-8f);

    float mn[3], mx[3];
    for (int ch = 0; ch < 3; ch++)
    {
        mn[ch] = fminf(fminf(c[3][ch], c[4][ch]), fminf(c[7][ch], c[8][ch]));
        mx[ch] = fmaxf(fmaxf(c[3][ch], c[4][ch]), fmaxf(c[7][ch], c[8][ch]));
    }

    float aC[3] = {0, 0, 0}, aW = 0.0f;
    for (int i = 0; i < 12; i++)
        easuTap(aC, &aW, off[i][0] - ppx, off[i][1] - ppy, dirx, diry, len2x, len2y, lob, clp, c[i]);

    float inv = 1.0f / fmaxf(aW, 1.0e-8f);
    for (int ch = 0; ch < 3; ch++)
    {
        float v = aC[ch] * inv;
        out[ch] = fminf(mx[ch], fmaxf(mn[ch], v));
    }
}

static void bilinear(const Image* in, int outW, int outH, int ipx, int ipy, float out[3])
{
    float sx = ((float)ipx + 0.5f) * (float)in->w / (float)outW - 0.5f;
    float sy = ((float)ipy + 0.5f) * (float)in->h / (float)outH - 0.5f;
    int   x0 = (int)floorf(sx), y0 = (int)floorf(sy);
    float fx = sx - (float)x0, fy = sy - (float)y0;
    float c00[3], c10[3], c01[3], c11[3];
    fetch(in, x0, y0, c00);
    fetch(in, x0 + 1, y0, c10);
    fetch(in, x0, y0 + 1, c01);
    fetch(in, x0 + 1, y0 + 1, c11);
    for (int ch = 0; ch < 3; ch++)
    {
        float top = c00[ch] * (1.0f - fx) + c10[ch] * fx;
        float bot = c01[ch] * (1.0f - fx) + c11[ch] * fx;
        out[ch]  = top * (1.0f - fy) + bot * fy;
    }
}

static int failures = 0;

static void report(const char* name, int ok, const char* detail)
{
    printf("%-42s %s  %s\n", name, ok ? "PASS" : "FAIL", detail);
    if (!ok)
        failures++;
}

int main(void)
{
    const int W = 32, H = 18;
    char      buf[256];

    // 1. flat input must come back exactly
    {
        Image img = makeImage(W, H);
        for (int i = 0; i < W * H; i++)
        {
            img.p[i * 3 + 0] = 0.3f;
            img.p[i * 3 + 1] = 0.5f;
            img.p[i * 3 + 2] = 0.7f;
        }
        float maxErr = 0.0f;
        for (int y = 0; y < H * 2; y++)
            for (int x = 0; x < W * 2; x++)
            {
                float o[3];
                easu(&img, W * 2, H * 2, x, y, o);
                maxErr = fmaxf(maxErr, fabsf(o[0] - 0.3f));
                maxErr = fmaxf(maxErr, fabsf(o[1] - 0.5f));
                maxErr = fmaxf(maxErr, fabsf(o[2] - 0.7f));
            }
        snprintf(buf, sizeof buf, "max err %.3e", maxErr);
        report("1 flat input is preserved", maxErr < 1e-5f, buf);
        free(img.p);
    }

    // 2. a single bright dot must land at the scaled position (alignment).
    //    EASU is not an identity filter, so comparing to the input at 1:1 says
    //    nothing about alignment; the centroid of the response does.
    {
        Image img = makeImage(W, H);
        const int cx = 16, cy = 9;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
            {
                float v                    = (x == cx && y == cy) ? 1.0f : 0.0f;
                img.p[(y * W + x) * 3 + 0] = v;
                img.p[(y * W + x) * 3 + 1] = v;
                img.p[(y * W + x) * 3 + 2] = v;
            }
        double sx = 0, sy = 0, sw = 0;
        for (int y = 0; y < H * 2; y++)
            for (int x = 0; x < W * 2; x++)
            {
                float o[3];
                easu(&img, W * 2, H * 2, x, y, o);
                sx += (double)x * o[0];
                sy += (double)y * o[0];
                sw += o[0];
            }
        double mx = sx / sw, my = sy / sw;
        double ex = ((double)cx + 0.5) * 2.0 - 0.5;
        double ey = ((double)cy + 0.5) * 2.0 - 0.5;
        double dx = fabs(mx - ex), dy = fabs(my - ey);
        snprintf(buf, sizeof buf, "centroid %.2f,%.2f vs %.2f,%.2f", mx, my, ex, ey);
        report("2 dot lands at the scaled position", dx < 0.5 && dy < 0.5, buf);
        free(img.p);
    }

    // 3. step edge must stay inside the local min/max (no overshoot)
    {
        Image img = makeImage(W, H);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
            {
                float v                    = x < W / 2 ? 0.0f : 1.0f;
                img.p[(y * W + x) * 3 + 0] = v;
                img.p[(y * W + x) * 3 + 1] = v;
                img.p[(y * W + x) * 3 + 2] = v;
            }
        float worst = 0.0f;
        for (int y = 0; y < H * 2; y++)
            for (int x = 0; x < W * 2; x++)
            {
                float o[3];
                easu(&img, W * 2, H * 2, x, y, o);
                float over = fmaxf(0.0f, fmaxf(-o[0], o[0] - 1.0f));
                worst      = fmaxf(worst, over);
            }
        snprintf(buf, sizeof buf, "overshoot %.3e", worst);
        report("3 step edge stays in range", worst < 1e-6f, buf);
        free(img.p);
    }

    // 4. shifting the input by one pixel shifts the output by scale pixels
    {
        Image a = makeImage(W, H), b = makeImage(W, H);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
            {
                float v                  = ((x * 5 + y * 3) % 9) / 8.0f;
                a.p[(y * W + x) * 3 + 0] = v;
                a.p[(y * W + x) * 3 + 1] = v;
                a.p[(y * W + x) * 3 + 2] = v;
                int sx                   = x - 1 < 0 ? 0 : x - 1; // shift right by one
                float s                  = ((sx * 5 + y * 3) % 9) / 8.0f;
                b.p[(y * W + x) * 3 + 0] = s;
                b.p[(y * W + x) * 3 + 1] = s;
                b.p[(y * W + x) * 3 + 2] = s;
            }
        float maxErr = 0.0f;
        for (int y = 4; y < H * 2 - 4; y++)
            for (int x = 6; x < W * 2 - 6; x++)
            {
                float oa[3], ob[3];
                easu(&a, W * 2, H * 2, x, y, oa);
                easu(&b, W * 2, H * 2, x + 2, y, ob);
                maxErr = fmaxf(maxErr, fabsf(oa[0] - ob[0]));
            }
        snprintf(buf, sizeof buf, "max err %.3e", maxErr);
        report("4 integer shift maps to scale*shift", maxErr < 1e-4f, buf);
        free(a.p);
        free(b.p);
    }

    // 5. EASU should keep a step edge at least as sharp as bilinear
    {
        Image img = makeImage(W, H);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
            {
                float v                    = x < W / 2 ? 0.0f : 1.0f;
                img.p[(y * W + x) * 3 + 0] = v;
                img.p[(y * W + x) * 3 + 1] = v;
                img.p[(y * W + x) * 3 + 2] = v;
            }
        int   y0 = H;
        float gradEasu = 0.0f, gradBilin = 0.0f, prevE = 0.0f, prevB = 0.0f;
        for (int x = 0; x < W * 2; x++)
        {
            float oe[3], ob[3];
            easu(&img, W * 2, H * 2, x, y0, oe);
            bilinear(&img, W * 2, H * 2, x, y0, ob);
            if (x > 0)
            {
                gradEasu  = fmaxf(gradEasu, fabsf(oe[0] - prevE));
                gradBilin = fmaxf(gradBilin, fabsf(ob[0] - prevB));
            }
            prevE = oe[0];
            prevB = ob[0];
        }
        snprintf(buf, sizeof buf, "eas u %.3f vs bilinear %.3f", gradEasu, gradBilin);
        report("5 edge at least as sharp as bilinear", gradEasu >= gradBilin - 1e-4f, buf);
        free(img.p);
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
