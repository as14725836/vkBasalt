// FidelityFX Super Resolution 1.0 - EASU (Edge Adaptive Spatial Upsampling)
//
// Port of FsrEasuF/FsrEasuTapF/FsrEasuSetF/FsrEasuCon from ffx_fsr1.h
// (GPUOpen, MIT licensed) to plain GLSL. The original uses gather4 per channel;
// GLSL has no gather, so gatherAt() rebuilds the same 2x2 footprint with four
// textureLod fetches and splits them into per channel vectors.
//
// The constants from FsrEasuCon() are derived in the shader: the input size
// comes from textureSize(), the output size is a specialization constant.
#version 450

layout(set = 0, binding = 0) uniform sampler2D img;

layout(constant_id = 0) const float outputWidth  = 1920.0;
layout(constant_id = 1) const float outputHeight = 1080.0;

layout(location = 0) in vec2 textureCoord;
layout(location = 0) out vec4 fragColor;

void gatherAt(vec2 p, out vec4 R, out vec4 G, out vec4 B)
{
    vec2 sz   = vec2(textureSize(img, 0));
    vec2 o    = 1.0 / sz;
    vec2 base = floor(p * sz - 0.5) + 0.5;
    vec4 s00  = textureLod(img, (base + vec2(0.0, 0.0)) * o, 0.0);
    vec4 s10  = textureLod(img, (base + vec2(1.0, 0.0)) * o, 0.0);
    vec4 s01  = textureLod(img, (base + vec2(0.0, 1.0)) * o, 0.0);
    vec4 s11  = textureLod(img, (base + vec2(1.0, 1.0)) * o, 0.0);
    R = vec4(s00.r, s10.r, s01.r, s11.r);
    G = vec4(s00.g, s10.g, s01.g, s11.g);
    B = vec4(s00.b, s10.b, s01.b, s11.b);
}

void easuTap(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len, float lob, float clp, vec3 c)
{
    vec2 v;
    v.x = (off.x * dir.x) + (off.y * dir.y);
    v.y = (off.x * (-dir.y)) + (off.y * dir.x);
    v *= len;
    float d2 = v.x * v.x + v.y * v.y;
    d2       = min(d2, clp);

    float wB = (2.0 / 5.0) * d2 + (-1.0);
    float wA = lob * d2 + (-1.0);
    wB *= wB;
    wA *= wA;
    wB = (25.0 / 16.0) * wB + (-(25.0 / 16.0 - 1.0));
    float w = wB * wA;
    aC += c * w;
    aW += w;
}

void easuSet(inout vec2 dir, inout float len, vec2 pp, int which, float lA, float lB, float lC, float lD, float lE)
{
    float w = 0.0;
    if (which == 0)
        w = (1.0 - pp.x) * (1.0 - pp.y);
    else if (which == 1)
        w = pp.x * (1.0 - pp.y);
    else if (which == 2)
        w = (1.0 - pp.x) * pp.y;
    else
        w = pp.x * pp.y;

    float dc   = lD - lC;
    float cb   = lC - lB;
    float lenX = max(abs(dc), abs(cb));
    lenX       = 1.0 / max(lenX, 1.0e-8);
    float dirX = lD - lB;
    dir.x += dirX * w;
    lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
    lenX *= lenX;
    len += lenX * w;

    float ec   = lE - lC;
    float ca   = lC - lA;
    float lenY = max(abs(ec), abs(ca));
    lenY       = 1.0 / max(lenY, 1.0e-8);
    float dirY = lE - lA;
    dir.y += dirY * w;
    lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
    lenY *= lenY;
    len += lenY * w;
}

void main()
{
    vec2 inSize  = vec2(textureSize(img, 0));
    vec2 outSize = vec2(outputWidth, outputHeight);
    vec2 o       = 1.0 / inSize;

    ivec2 ip = ivec2(gl_FragCoord.xy);

    // FsrEasuCon: con0 = {inW/outW, inH/outH, 0.5*inW/outW-0.5, 0.5*inH/outH-0.5}
    vec2 con0xy = inSize / outSize;
    vec2 con0zw = 0.5 * inSize / outSize - 0.5;

    vec2 pp = vec2(ip) * con0xy + con0zw;
    vec2 fp = floor(pp);
    pp -= fp;

    // con1 = {1/inW, 1/inH, -1/inW, -1/inH}, con2 = {-1/inW, 2/inH, 1/inW, 2/inH}, con3 = {0, 4/inH, 0, 0}
    vec2 p0 = fp * o + vec2(-1.0, -1.0) * o;
    vec2 p1 = p0 + vec2(-1.0, 2.0) * o;
    vec2 p2 = p0 + vec2(1.0, 2.0) * o;
    vec2 p3 = p0 + vec2(0.0, 4.0) * o;

    vec4 bczzR, bczzG, bczzB;
    vec4 ijfeR, ijfeG, ijfeB;
    vec4 klhgR, klhgG, klhgB;
    vec4 zzonR, zzonG, zzonB;
    gatherAt(p0, bczzR, bczzG, bczzB);
    gatherAt(p1, ijfeR, ijfeG, ijfeB);
    gatherAt(p2, klhgR, klhgG, klhgB);
    gatherAt(p3, zzonR, zzonG, zzonB);

    vec4 bczzL = bczzB * 0.5 + (bczzR * 0.5 + bczzG);
    vec4 ijfeL = ijfeB * 0.5 + (ijfeR * 0.5 + ijfeG);
    vec4 klhgL = klhgB * 0.5 + (klhgR * 0.5 + klhgG);
    vec4 zzonL = zzonB * 0.5 + (zzonR * 0.5 + zzonG);

    float bL = bczzL.x;
    float cL = bczzL.y;
    float iL = ijfeL.x;
    float jL = ijfeL.y;
    float fL = ijfeL.z;
    float eL = ijfeL.w;
    float kL = klhgL.x;
    float lL = klhgL.y;
    float hL = klhgL.z;
    float gL = klhgL.w;
    float oL = zzonL.z;
    float nL = zzonL.w;

    vec2  dir = vec2(0.0);
    float len = 0.0;
    easuSet(dir, len, pp, 0, bL, eL, fL, gL, jL);
    easuSet(dir, len, pp, 1, cL, fL, gL, hL, kL);
    easuSet(dir, len, pp, 2, fL, iL, jL, kL, nL);
    easuSet(dir, len, pp, 3, gL, jL, kL, lL, oL);

    vec2  dir2 = dir * dir;
    float dirR = dir2.x + dir2.y;
    bool  zro  = dirR < (1.0 / 32768.0);
    dirR       = inversesqrt(max(dirR, 1.0e-8));
    dirR       = zro ? 1.0 : dirR;
    dir.x      = zro ? 1.0 : dir.x;
    dir *= dirR;

    len = len * 0.5;
    len *= len;

    float stretch = dot(dir, dir) * (1.0 / max(max(abs(dir.x), abs(dir.y)), 1.0e-8));
    vec2  len2    = vec2(1.0 + (stretch - 1.0) * len, 1.0 + (-0.5) * len);
    float lob     = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    float clp     = 1.0 / max(lob, 1.0e-8);

    vec3 min4 = min(min(vec3(ijfeR.z, ijfeG.z, ijfeB.z), vec3(klhgR.w, klhgG.w, klhgB.w)), vec3(ijfeR.y, ijfeG.y, ijfeB.y));
    min4      = min(min4, vec3(klhgR.x, klhgG.x, klhgB.x));
    vec3 max4 = max(max(vec3(ijfeR.z, ijfeG.z, ijfeB.z), vec3(klhgR.w, klhgG.w, klhgB.w)), vec3(ijfeR.y, ijfeG.y, ijfeB.y));
    max4      = max(max4, vec3(klhgR.x, klhgG.x, klhgB.x));

    vec3  aC = vec3(0.0);
    float aW = 0.0;
    easuTap(aC, aW, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, vec3(bczzR.x, bczzG.x, bczzB.x));
    easuTap(aC, aW, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, vec3(bczzR.y, bczzG.y, bczzB.y));
    easuTap(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, vec3(ijfeR.x, ijfeG.x, ijfeB.x));
    easuTap(aC, aW, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, vec3(ijfeR.y, ijfeG.y, ijfeB.y));
    easuTap(aC, aW, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, vec3(ijfeR.z, ijfeG.z, ijfeB.z));
    easuTap(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, vec3(ijfeR.w, ijfeG.w, ijfeB.w));
    easuTap(aC, aW, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, vec3(klhgR.x, klhgG.x, klhgB.x));
    easuTap(aC, aW, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, vec3(klhgR.y, klhgG.y, klhgB.y));
    easuTap(aC, aW, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, vec3(klhgR.z, klhgG.z, klhgB.z));
    easuTap(aC, aW, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, vec3(klhgR.w, klhgG.w, klhgB.w));
    easuTap(aC, aW, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, vec3(zzonR.z, zzonG.z, zzonB.z));
    easuTap(aC, aW, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, vec3(zzonR.w, zzonG.w, zzonB.w));

    vec3 pix = min(max4, max(min4, aC * (1.0 / max(aW, 1.0e-8))));
    float alpha = textureLod(img, textureCoord, 0.0).a;
    fragColor = vec4(pix, alpha);
}
