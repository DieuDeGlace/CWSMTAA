#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "smaa/AreaTex.h"
#include "smaa/SearchTex.h"
#include "smaa/SMAAEmbedded.h"
#include "GpuPassTimer9.h"

// D3D9 port of Steam TemporalAA's color rejection and fine-detail resolve.
// Native depth is never replaced. Resolve runs at the world/HUD boundary.
static HMODULE self;
static IDirect3DDevice9 *dev;
static GpuPassTimer9 gpuTimer;
static void **vt;
template <class T> static void drop(T *&p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}
static std::wstring directory() {
    wchar_t p[32768];
    GetModuleFileNameW(self, p, 32768);
    std::wstring s = p;
    s.resize(s.find_last_of(L'\\'));
    if (s.substr(s.find_last_of(L'\\') + 1) == L"Mods")
        s = s.substr(0, s.find_last_of(L'\\')) + L"\\graphics-rebuild";
    return s;
}
static void log(const char *s) {
    FILE *f = nullptr;
    _wfopen_s(&f, (directory() + L"\\AlphaTemporal.log").c_str(), L"a");
    if (f) {
        fprintf(f, "%s\n", s);
        fclose(f);
    }
}
using DIP = HRESULT(WINAPI *)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
using CLEAR = HRESULT(WINAPI *)(IDirect3DDevice9 *, DWORD, const D3DRECT *, DWORD, D3DCOLOR, float,
                                DWORD);
using RESET = HRESULT(WINAPI *)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
using PRESENT = HRESULT(WINAPI *)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND,
                                  const RGNDATA *);
static DIP oldDIP;
static CLEAR oldClear;
static RESET oldReset;
static PRESENT oldPresent;
static bool enabled = true, internal = false, ready = false, valid = false, worldSeen = false,
            finished = false;
static UINT width = 0, height = 0, frame = 0;
static float weight = .65f, jx = 0, jy = 0;
static IDirect3DTexture9 *scene = nullptr, *history = nullptr;
static IDirect3DPixelShader9 *shader = nullptr;
static IDirect3DTexture9 *edgesTexture = nullptr, *weightsTexture = nullptr,
                         *spatialTexture = nullptr, *areaLookup = nullptr, *searchLookup = nullptr;
static IDirect3DPixelShader9 *smaaShaders[3]{}, *softnessShader = nullptr;
static bool useSMAA = false;
static float displaySoftness = 0, resolveBlur = .12f, jitterScale = .75f;
static float previousCamera[4]{}, previousProjection[2]{};
static bool cameraKnown = false;
static std::map<IDirect3DVertexShader9 *, bool> worldShaders;
static const char *hlsl = R"HLSL(
sampler2D scene:register(s0);sampler2D history:register(s1);
float4 settings:register(c0);float4 offsets:register(c1);
float4 resolve(float2 uv:TEXCOORD0):COLOR0{
 float2 original=uv;uv+=offsets.xy;float4 current=tex2D(scene,uv);
 float3 lo=current.rgb,hi=current.rgb,left=current.rgb,right=current.rgb,up=current.rgb,down=current.rgb;
 [unroll]for(int y=-1;y<=1;y++)[unroll]for(int x=-1;x<=1;x++){
  float3 c=tex2D(scene,uv+float2(x,y)*settings.xy).rgb;lo=min(lo,c);hi=max(hi,c);
  if(y==0&&x==-1)left=c;if(y==0&&x==1)right=c;if(x==0&&y==-1)up=c;if(x==0&&y==1)down=c;
 }
 float3 old=tex2D(history,original).rgb;
 float disagreement=max(abs(old.r-current.r),max(abs(old.g-current.g),abs(old.b-current.b)));
 float3 lr=(left-current.rgb)*(right-current.rgb),ud=(up-current.rgb)*(down-current.rgb);
 float thin=saturate(24*sqrt(max(0,max(max(lr.r,lr.g),max(lr.b,max(max(ud.r,ud.g),ud.b))))));
 float confidence=1-smoothstep(.035,lerp(.16,.30,thin),disagreement);
 float accumulation=lerp(settings.z,max(settings.z,.78),thin);
 float3 filtered=lerp(current.rgb,(current.rgb*4+left+right+up+down)/8,saturate(offsets.w));
 return float4(lerp(filtered,clamp(old,lo,hi),accumulation*settings.w*confidence),current.a);
}
float4 soften(float2 uv:TEXCOORD0):COLOR0{
 float4 now=tex2D(scene,uv);float3 sum=0;
 [unroll]for(int y=-1;y<=1;y++)[unroll]for(int x=-1;x<=1;x++)sum+=tex2D(scene,uv+float2(x,y)*settings.xy).rgb*(x==0?2:1)*(y==0?2:1);
 return float4(lerp(now.rgb,sum/16,saturate(offsets.z)),now.a);
})HLSL";
static float halton(UINT i, UINT base) {
    float f = 1, r = 0;
    while (i) {
        f /= base;
        r += f * (i % base);
        i /= base;
    }
    return r - .5f;
}
static void cleanup() {
    gpuTimer.reset();
    drop(scene);
    drop(history);
    drop(shader);
    drop(edgesTexture);
    drop(weightsTexture);
    drop(spatialTexture);
    drop(areaLookup);
    drop(searchLookup);
    for (auto &p : smaaShaders)
        drop(p);
    drop(softnessShader);
    ready = valid = false;
    width = height = 0;
}
static bool initialize() {
    IDirect3DSurface9 *bb = nullptr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)))
        return false;
    D3DSURFACE_DESC desc{};
    bb->GetDesc(&desc);
    drop(bb);
    if (desc.MultiSampleType != D3DMULTISAMPLE_NONE) {
        log("Native AA must be off for this prototype.");
        return false;
    }
    if (ready && width == desc.Width && height == desc.Height)
        return true;
    cleanup();
    width = desc.Width;
    height = desc.Height;
    bool ok = SUCCEEDED(dev->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, desc.Format,
                                           D3DPOOL_DEFAULT, &scene, nullptr)) &&
              SUCCEEDED(dev->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, desc.Format,
                                           D3DPOOL_DEFAULT, &history, nullptr));
    HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    auto compile =
        compiler ? (decltype(&D3DCompile))GetProcAddress(compiler, "D3DCompile") : nullptr;
    ID3DBlob *code = nullptr, *errors = nullptr;
    auto pixelShader = [&](const char *source, const char *entry, IDirect3DPixelShader9 **out) {
        ID3DBlob *c = nullptr, *e = nullptr;
        bool result = compile && SUCCEEDED(compile(source, strlen(source), "AlphaTemporal", nullptr,
                                                   nullptr, entry, "ps_3_0",
                                                   D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &c, &e));
        if (e)
            log((char *)e->GetBufferPointer());
        if (result)
            result = SUCCEEDED(dev->CreatePixelShader((DWORD *)c->GetBufferPointer(), out));
        drop(c);
        drop(e);
        return result;
    };
    ok = ok && pixelShader(hlsl, "resolve", &shader);
    if (ok && displaySoftness > 0)
        ok = pixelShader(hlsl, "soften", &softnessShader);
    const char *entries[] = {"edges", "weights", "neighborhood"};
    for (size_t i = 0; useSMAA && i < std::size(smaaShaders) && ok; i++)
        ok = pixelShader(smaaSource, entries[i], &smaaShaders[i]);
    for (auto output : {&edgesTexture, &weightsTexture, &spatialTexture})
        if (ok && useSMAA)
            ok = SUCCEEDED(
                dev->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET,
                                   output == &spatialTexture ? desc.Format : D3DFMT_A8R8G8B8,
                                   D3DPOOL_DEFAULT, output, nullptr));
    auto lookup = [&](UINT w, UINT h, D3DFORMAT f, UINT pitch, const unsigned char *bytes,
                      IDirect3DTexture9 **out) {
        if (FAILED(dev->CreateTexture(w, h, 1, 0, f, D3DPOOL_MANAGED, out, nullptr)))
            return false;
        D3DLOCKED_RECT r{};
        if (FAILED((*out)->LockRect(0, &r, nullptr, 0)))
            return false;
        for (UINT y = 0; y < h; y++)
            memcpy((char *)r.pBits + y * r.Pitch, bytes + y * pitch, pitch);
        (*out)->UnlockRect(0);
        return true;
    };
    if (useSMAA)
        ok = ok &&
             lookup(AREATEX_WIDTH, AREATEX_HEIGHT, D3DFMT_A8L8, AREATEX_PITCH, areaTexBytes,
                    &areaLookup) &&
             lookup(SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, D3DFMT_L8, SEARCHTEX_PITCH, searchTexBytes,
                    &searchLookup);
    if (compiler)
        FreeLibrary(compiler);
    if (!ok) {
        cleanup();
        return false;
    }
    ready = true;
    log("Steam temporal resolve + SMAA Ultra + display-only softness initialized; native depth retained.");
    return true;
}
static HRESULT quad() {
    struct Vertex {
        float x, y, z, w, u, v;
    };
    Vertex q[] = {{-.5f, -.5f, 0, 1, 0, 0},
                  {width - .5f, -.5f, 0, 1, 1, 0},
                  {-.5f, height - .5f, 0, 1, 0, 1},
                  {width - .5f, height - .5f, 0, 1, 1, 1}};
    return dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vertex));
}
// Post filters are display-only: temporal history remains the unblurred resolve.
// This runs before native HUD rendering, so no HUD mask or replay is needed.
static HRESULT display(IDirect3DSurface9 *target) {
    if (!useSMAA && displaySoftness <= 0)
        return S_OK;
    HRESULT hr = dev->BeginScene();
    if (FAILED(hr))
        return hr;
    IDirect3DTexture9 *input = history;
    IDirect3DSurface9 *edge = nullptr, *blend = nullptr, *spatial = nullptr;
    for (int i = 0; i < 6; i++) {
        dev->SetTexture(i, nullptr);
        dev->SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(i, D3DSAMP_SRGBTEXTURE, FALSE);
    }
    if (useSMAA) {
        edgesTexture->GetSurfaceLevel(0, &edge);
        weightsTexture->GetSurfaceLevel(0, &blend);
        spatialTexture->GetSurfaceLevel(0, &spatial);
        float metrics[4] = {1.f / width, 1.f / height, (float)width, (float)height};
        dev->SetPixelShaderConstantF(10, metrics, 1);
        dev->SetRenderTarget(0, edge);
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0);
        dev->SetTexture(0, history);
        dev->SetPixelShader(smaaShaders[0]);
        hr = quad();
        if (SUCCEEDED(hr)) {
            dev->SetRenderTarget(0, blend);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0);
            dev->SetTexture(1, edgesTexture);
            dev->SetTexture(2, areaLookup);
            dev->SetTexture(3, searchLookup);
            dev->SetPixelShader(smaaShaders[1]);
            hr = quad();
        }
        if (SUCCEEDED(hr)) {
            dev->SetTexture(1, nullptr);
            dev->SetTexture(2, nullptr);
            dev->SetTexture(3, nullptr);
            dev->SetRenderTarget(0, spatial);
            dev->SetTexture(4, weightsTexture);
            dev->SetPixelShader(smaaShaders[2]);
            hr = quad();
            input = spatialTexture;
        }
    }
    for (int i = 0; i < 6; i++)
        dev->SetTexture(i, nullptr);
    dev->SetRenderTarget(0, target);
    if (SUCCEEDED(hr) && displaySoftness > 0) {
        float offsets[4] = {0, 0, displaySoftness, 0};
        dev->SetPixelShaderConstantF(1, offsets, 1);
        dev->SetTexture(0, input);
        dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        dev->SetPixelShader(softnessShader);
        hr = quad();
        dev->SetTexture(0, nullptr);
    }
    dev->EndScene();
    if (SUCCEEDED(hr) && displaySoftness <= 0 && useSMAA)
        hr = dev->StretchRect(spatial, nullptr, target, nullptr, D3DTEXF_NONE);
    drop(edge);
    drop(blend);
    drop(spatial);
    return hr;
}
static bool backbuffer() {
    IDirect3DSurface9 *rt = nullptr, *bb = nullptr;
    dev->GetRenderTarget(0, &rt);
    dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
    bool same = rt && rt == bb;
    drop(rt);
    drop(bb);
    return same;
}
static bool apply() {
    if (!enabled || !worldSeen || finished || internal || !initialize())
        return false;
    finished = true;
    IDirect3DStateBlock9 *state = nullptr;
    IDirect3DSurface9 *rt = nullptr, *ds = nullptr, *copy = nullptr, *old = nullptr;
    D3DVIEWPORT9 vp{};
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &state)))
        return false;
    dev->GetRenderTarget(0, &rt);
    dev->GetDepthStencilSurface(&ds);
    dev->GetViewport(&vp);
    scene->GetSurfaceLevel(0, &copy);
    history->GetSurfaceLevel(0, &old);
    gpuTimer.begin(dev, "Alpha TAA (copy/resolve)", log);
    // D3D9 forbids StretchRect inside BeginScene. Suspend and resume the native
    // scene at its HUD boundary; keep BeginScene/EndScene balanced on failures.
    bool resume = SUCCEEDED(dev->EndScene());
    HRESULT hr = dev->StretchRect(rt, nullptr, copy, nullptr, D3DTEXF_NONE);
    if (SUCCEEDED(hr) && !valid)
        hr = dev->StretchRect(copy, nullptr, old, nullptr, D3DTEXF_NONE);
    internal = true;
    bool begun = false;
    if (SUCCEEDED(hr)) {
        hr = dev->BeginScene();
        begun = SUCCEEDED(hr);
    }
    if (begun) {
        dev->SetDepthStencilSurface(nullptr);
        dev->SetVertexShader(nullptr);
        dev->SetPixelShader(shader);
        dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
        dev->SetRenderState(D3DRS_ZENABLE, FALSE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
        dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
        IDirect3DTexture9 *textures[] = {scene, history};
        for (int i = 0; i < 2; i++) {
            dev->SetTexture(i, textures[i]);
            dev->SetSamplerState(i, D3DSAMP_MINFILTER, i ? D3DTEXF_POINT : D3DTEXF_LINEAR);
            dev->SetSamplerState(i, D3DSAMP_MAGFILTER, i ? D3DTEXF_POINT : D3DTEXF_LINEAR);
            dev->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
            dev->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            dev->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            dev->SetSamplerState(i, D3DSAMP_SRGBTEXTURE, FALSE);
        }
        float constants[8] = {1.f / width, 1.f / height, weight, valid ? 1.f : 0.f,
                              jx / width,  jy / height,  0,      resolveBlur};
        dev->SetPixelShaderConstantF(0, constants, 2);
        struct Vertex {
            float x, y, z, w, u, v;
        };
        Vertex q[] = {{-.5f, -.5f, 0, 1, 0, 0},
                      {width - .5f, -.5f, 0, 1, 1, 0},
                      {-.5f, height - .5f, 0, 1, 0, 1},
                      {width - .5f, height - .5f, 0, 1, 1, 1}};
        gpuTimer.split();
        hr = dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vertex));
        dev->SetTexture(0, nullptr);
        dev->SetTexture(1, nullptr);
        dev->EndScene();
        if (SUCCEEDED(hr))
            hr = dev->StretchRect(rt, nullptr, old, nullptr, D3DTEXF_NONE);
    }
    if (SUCCEEDED(hr))
        hr = display(rt);
    if (resume) {
        HRESULT resumed = dev->BeginScene();
        if (FAILED(resumed))
            hr = resumed;
    }
    state->Apply();
    dev->SetRenderTarget(0, rt);
    dev->SetDepthStencilSurface(ds);
    dev->SetViewport(&vp);
    internal = false;
    gpuTimer.end();
    valid = SUCCEEDED(hr);
    drop(copy);
    drop(old);
    drop(rt);
    drop(ds);
    drop(state);
    return valid;
}
static bool worldShader(IDirect3DVertexShader9 *vs) {
    auto it = worldShaders.find(vs);
    if (it != worldShaders.end())
        return it->second;
    UINT size = 0;
    bool good = false;
    if (vs && SUCCEEDED(vs->GetFunction(nullptr, &size)) && size < 65536) {
        std::vector<char> b(size);
        if (SUCCEEDED(vs->GetFunction(b.data(), &size))) {
            auto has = [&](const char *n) {
                return std::search(b.begin(), b.end(), n, n + strlen(n) + 1) != b.end();
            };
            good = has("worldViewProjMatrix") && has("worldMatrix") && has("lightDirection");
        }
    }
    worldShaders[vs] = good;
    return good;
}
static HRESULT WINAPI dip(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, INT b, UINT m, UINT n, UINT s,
                          UINT p) {
    float saved[16]{};
    bool jittered = false;
    if (!internal && enabled && !finished && backbuffer() && SUCCEEDED(d->TestCooperativeLevel())) {
        IDirect3DVertexShader9 *vs = nullptr;
        d->GetVertexShader(&vs);
        bool accepted = worldShader(vs);
        drop(vs);
        if (accepted && SUCCEEDED(d->GetVertexShaderConstantF(20, saved, 4)) &&
            fabs(saved[12]) + fabs(saved[13]) + fabs(saved[14]) > 1e-5f) {
            if (!worldSeen) {
                float cam[4]{};
                d->GetVertexShaderConstantF(36, cam, 1);
                if (cameraKnown) {
                    for (int i = 0; i < 3; i++)
                        if (fabs(cam[i] - previousCamera[i]) > 8)
                            valid = false;
                    if (fabs(saved[0] - previousProjection[0]) > .35f ||
                        fabs(saved[5] - previousProjection[1]) > .35f)
                        valid = false;
                }
                memcpy(previousCamera, cam, sizeof(cam));
                previousProjection[0] = saved[0];
                previousProjection[1] = saved[5];
                cameraKnown = true;
                worldSeen = true;
            }
            if (initialize()) {
                float j[16];
                memcpy(j, saved, sizeof(j));
                for (int c = 0; c < 4; c++) {
                    j[c] += 2 * jx / width * j[12 + c];
                    j[4 + c] -= 2 * jy / height * j[12 + c];
                }
                d->SetVertexShaderConstantF(20, j, 4);
                jittered = true;
            }
        }
    }
    HRESULT hr = oldDIP(d, t, b, m, n, s, p);
    if (jittered)
        d->SetVertexShaderConstantF(20, saved, 4);
    return hr;
}
static HRESULT WINAPI clear(IDirect3DDevice9 *d, DWORD n, const D3DRECT *r, DWORD flags, D3DCOLOR c,
                            float z, DWORD s) {
    if (!internal && worldSeen && !finished && (flags & D3DCLEAR_ZBUFFER) &&
        !(flags & D3DCLEAR_TARGET) && backbuffer() && SUCCEEDED(d->TestCooperativeLevel()))
        apply();
    return oldClear(d, n, r, flags, c, z, s);
}
static HRESULT WINAPI present(IDirect3DDevice9 *d, const RECT *a, const RECT *b, HWND w,
                              const RGNDATA *r) {
    if (worldSeen && !finished)
        valid = false;
    D3DDEVICE_CREATION_PARAMETERS cp{};
    d->GetCreationParameters(&cp);
    if (GetForegroundWindow() == cp.hFocusWindow && (GetAsyncKeyState(VK_F8) & 1)) {
        enabled = !enabled;
        valid = false;
    }
    if (GetForegroundWindow() == cp.hFocusWindow && (GetAsyncKeyState(VK_F9) & 1)) {
        useSMAA = !useSMAA;
        cleanup(); // Optional display resources are created only when requested.
        log(useSMAA ? "SMAA enabled." : "SMAA disabled; display softness retained.");
    }
    worldSeen = finished = false;
    frame++;
    jx = halton(frame % 8 + 1, 2) * jitterScale;
    jy = halton(frame % 8 + 1, 3) * jitterScale;
    HRESULT hr = oldPresent(d, a, b, w, r);
    if (FAILED(hr))
        valid = false;
    return hr;
}
static HRESULT WINAPI reset(IDirect3DDevice9 *d, D3DPRESENT_PARAMETERS *p) {
    cleanup();
    worldShaders.clear();
    cameraKnown = worldSeen = finished = false;
    log("Temporal resources released before device reset.");
    return oldReset(d, p);
}
extern "C" __declspec(dllexport) BOOL WINAPI QueryAlphaSceneJitter(float *xy) {
    if (!xy)
        return FALSE;
    xy[0] = xy[1] = 0;
    if (!enabled || !ready || !width || !height)
        return FALSE;
    xy[0] = jx / width;
    xy[1] = jy / height;
    return TRUE;
}
static void *exchange(int slot, void *fn) {
    DWORD p = 0;
    if (!VirtualProtect(vt + slot, 4, PAGE_EXECUTE_READWRITE, &p))
        return nullptr;
    void *old = InterlockedExchangePointer(vt + slot, fn);
    DWORD ignore;
    VirtualProtect(vt + slot, 4, p, &ignore);
    return old;
}
static bool read(DWORD a, void *b, size_t n) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(uintptr_t)a, b, n, &got) && got == n;
}
static DWORD pointer(DWORD a) {
    DWORD v = 0;
    read(a, &v, 4);
    return v;
}
static DWORD WINAPI worker(void *) {
    auto ini = directory() + L"\\AlphaTemporal.ini";
    weight =
        std::clamp((int)GetPrivateProfileIntW(L"TAA", L"HistoryPercent", 65, ini.c_str()), 0, 85) *
        .01f;
    useSMAA = GetPrivateProfileIntW(L"TAA", L"SMAAEnabled", 0, ini.c_str()) != 0;
    wchar_t value[32]{};
    GetPrivateProfileStringW(L"TAA", L"DisplaySoftness", L"0", value, 32, ini.c_str());
    displaySoftness = std::clamp((float)_wtof(value), 0.f, 1.f);
    GetPrivateProfileStringW(L"TAA", L"ResolveBlur", L"0.12", value, 32, ini.c_str());
    resolveBlur = std::clamp((float)_wtof(value), 0.f, 1.f);
    GetPrivateProfileStringW(L"TAA", L"JitterScale", L"0.75", value, 32, ini.c_str());
    jitterScale = std::clamp((float)_wtof(value), 0.f, 1.f);
    DWORD base = (DWORD)(uintptr_t)GetModuleHandleW(nullptr);
    IMAGE_DOS_HEADER dos{};
    read(base, &dos, sizeof(dos));
    IMAGE_NT_HEADERS32 nt{};
    read(base + dos.e_lfanew, &nt, sizeof(nt));
    DWORD section = base + dos.e_lfanew + 24 + nt.FileHeader.SizeOfOptionalHeader;
    for (int attempt = 0; attempt < 180; attempt++) {
        std::set<DWORD> candidates;
        auto add = [&](DWORD a, size_t size) {
            if (size > 16 * 1024 * 1024)
                return;
            std::vector<DWORD> b(size / 4);
            if (read(a, b.data(), b.size() * 4))
                for (DWORD p : b)
                    if (p > 0x10000 && p < 0x7fff0000 && p % 4 == 0)
                        candidates.insert(p);
        };
        for (int i = 0; i < nt.FileHeader.NumberOfSections; i++) {
            IMAGE_SECTION_HEADER s{};
            if (read(section + i * sizeof(s), &s, sizeof(s)) && !memcmp(s.Name, ".data", 5))
                add(base + s.VirtualAddress, s.Misc.VirtualSize);
        }
        auto roots = candidates;
        for (DWORD p : roots)
            add(p, 16384);
        for (DWORD p : candidates) {
            DWORD model = pointer(p + 0x988 * 4), dims[3]{};
            if (!read(model + 0x44, dims, 12) || dims[0] != 16 || dims[1] != 16 || dims[2] != 25)
                continue;
            dev = (IDirect3DDevice9 *)(uintptr_t)pointer(model + 0x58);
            vt = (void **)(uintptr_t)pointer((DWORD)(uintptr_t)dev);
            if (!vt)
                continue;
            jx = halton(1, 2);
            jy = halton(1, 3);
            oldClear = (CLEAR)exchange(43, (void *)clear);
            oldReset = (RESET)exchange(16, (void *)reset);
            oldDIP = (DIP)exchange(82, (void *)dip);
            oldPresent = (PRESENT)exchange(17, (void *)present);
            log("AlphaTemporal Steam-stable test attached (fused resolve blur, reduced jitter, optional display filters off by default).");
            return 0;
        }
        Sleep(1000);
    }
    log("Alpha device not found.");
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE m, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        self = m;
        DisableThreadLibraryCalls(m);
        wchar_t p[32768];
        GetModuleFileNameW(nullptr, p, 32768);
        auto name = wcsrchr(p, L'\\');
        if (name && !_wcsicmp(name + 1, L"Cube.exe")) {
            HANDLE t = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
            if (t)
                CloseHandle(t);
        }
    }
    return TRUE;
}
