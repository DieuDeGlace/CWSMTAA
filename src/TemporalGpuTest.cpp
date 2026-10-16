#include "AlphaTemporal.cpp"
#include <cassert>
static void edgeStabilityTest(D3DFORMAT format) {
    useSMAA = false;
    displaySoftness = 0;
    auto variation = [&](float stability, bool thin) {
        cleanup();
        edgeStability = stability;
        assert(initialize());
        IDirect3DSurface9 *bb = nullptr, *cpu = nullptr;
        dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
        assert(SUCCEEDED(dev->CreateOffscreenPlainSurface(width, height, format, D3DPOOL_SYSTEMMEM,
                                                          &cpu, nullptr)));
        int lo = 255, hi = 0;
        for (float offset : {-.375f, -.25f, -.125f, 0.f, .125f, .25f, .375f}) {
            assert(SUCCEEDED(dev->BeginScene()));
            assert(SUCCEEDED(
                dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff000000, 1, 0)));
            D3DRECT shape = thin ? D3DRECT{64, 48, 65, 49} : D3DRECT{0, 0, 65, 96};
            assert(SUCCEEDED(dev->Clear(1, &shape, D3DCLEAR_TARGET, 0xffffffff, 1, 0)));
            jx = offset;
            jy = 0;
            worldSeen = true;
            finished = false;
            valid = false;
            assert(apply());
            assert(SUCCEEDED(dev->EndScene()));
            assert(SUCCEEDED(dev->GetRenderTargetData(bb, cpu)));
            D3DLOCKED_RECT lock{};
            assert(SUCCEEDED(cpu->LockRect(&lock, nullptr, D3DLOCK_READONLY)));
            int value = ((BYTE *)lock.pBits)[48 * lock.Pitch + 64 * 4];
            lo = std::min(lo, value);
            hi = std::max(hi, value);
            int flat = ((BYTE *)lock.pBits)[4 * lock.Pitch + 4 * 4];
            assert(flat == (thin ? 0 : 255));
            cpu->UnlockRect();
        }
        drop(cpu);
        drop(bb);
        return hi - lo;
    };
    for (bool thin : {true, false}) {
        int baseline = variation(0, thin), stabilized = variation(.24f, thin);
        assert(stabilized < baseline - 2);
        printf("High-contrast %s edge intensity range: %d -> %d; flat pixels unchanged: PASS\n",
               thin ? "thin-model" : "silhouette", baseline, stabilized);
    }
    edgeStability = .24f;
}
// Runs `frames` resolves with the game's jitter sequence and carried history.
// `paint(frame)` fills the scene; returns each frame's displayed BGRA pixels.
template <class Paint>
static std::vector<std::vector<unsigned char>> sequence(D3DFORMAT format, int frames, Paint paint) {
    useSMAA = false;
    displaySoftness = 0;
    cleanup();
    assert(initialize());
    IDirect3DSurface9 *bb = nullptr, *cpu = nullptr;
    dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
    assert(SUCCEEDED(
        dev->CreateOffscreenPlainSurface(width, height, format, D3DPOOL_SYSTEMMEM, &cpu, nullptr)));
    std::vector<std::vector<unsigned char>> out;
    valid = false;
    for (int f = 0; f < frames; f++) {
        assert(SUCCEEDED(dev->BeginScene()));
        paint(f);
        jx = halton(f % 8 + 1, 2) * jitterScale;
        jy = halton(f % 8 + 1, 3) * jitterScale;
        worldSeen = true;
        finished = false;
        assert(apply());
        assert(SUCCEEDED(dev->EndScene()));
        assert(SUCCEEDED(dev->GetRenderTargetData(bb, cpu)));
        D3DLOCKED_RECT lock{};
        assert(SUCCEEDED(cpu->LockRect(&lock, nullptr, D3DLOCK_READONLY)));
        std::vector<unsigned char> pixels(width * height * 4);
        for (UINT y = 0; y < height; y++)
            memcpy(pixels.data() + y * width * 4, (BYTE *)lock.pBits + y * lock.Pitch, width * 4);
        cpu->UnlockRect();
        out.push_back(std::move(pixels));
    }
    drop(cpu);
    drop(bb);
    return out;
}
static void fill(int x0, int y0, int x1, int y1, D3DCOLOR color) {
    D3DRECT r{std::max(x0, 0), std::max(y0, 0), std::min(x1, (int)width),
              std::min(y1, (int)height)};
    if (r.x1 < r.x2 && r.y1 < r.y2)
        assert(SUCCEEDED(dev->Clear(1, &r, D3DCLEAR_TARGET, color, 1, 0)));
}
// Steam port check: a static slanted edge under the 8-phase jitter must settle
// instead of shimmering once history is kept inside the neighborhood.
static double shimmerTest(D3DFORMAT format) {
    auto frames = sequence(format, 40, [](int) {
        fill(0, 0, width, height, 0xff202020);
        for (int y = 0; y < (int)height; y++)
            fill(0, y, 40 + y / 3, y + 1, 0xffe0e0e0);
    });
    double total = 0;
    int count = 0;
    for (int y = 8; y < (int)height - 8; y++)
        for (int x = 36 + y / 3; x <= 44 + y / 3; x++) {
            int lo = 255, hi = 0;
            for (size_t f = 24; f < frames.size(); f++) {
                int v = frames[f][(y * width + x) * 4 + 1];
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            total += hi - lo;
            count++;
        }
    return total / count;
}
// Steam port check: a panning grass/stone edge over a sky horizon must only
// show mixes of real scene colors (no off-hue fringes from box corners).
static double fringeTest(D3DFORMAT format) {
    const int colors[3][3] = {{90, 150, 230}, {50, 150, 40}, {130, 120, 110}}; // sky, grass, stone
    auto rgb = [](const int *c) { return D3DCOLOR_XRGB(c[0], c[1], c[2]); };
    auto frames = sequence(format, 32, [&](int f) {
        fill(0, 0, width, 48, rgb(colors[0]));
        fill(0, 48, 40 + f, height, rgb(colors[1]));
        fill(40 + f, 48, width, height, rgb(colors[2]));
    });
    auto segment = [](const double *p, const int *a, const int *b) {
        double ab[3], ap[3], t = 0, len = 0;
        for (int i = 0; i < 3; i++) {
            ab[i] = b[i] - a[i];
            ap[i] = p[i] - a[i];
            t += ab[i] * ap[i];
            len += ab[i] * ab[i];
        }
        t = len > 0 ? std::clamp(t / len, 0.0, 1.0) : 0;
        double d = 0;
        for (int i = 0; i < 3; i++)
            d += (ap[i] - ab[i] * t) * (ap[i] - ab[i] * t);
        return sqrt(d);
    };
    // Any blend of the three scene colors is legitimate (the corner where the
    // edge meets the horizon mixes all three); a per-channel clamp can instead
    // land on a box corner outside that triangle. Distance to the triangle:
    // inside it, the distance to its plane; otherwise to its nearest edge.
    auto triangle = [&](const double *p) {
        double u[3], v[3], w[3];
        for (int i = 0; i < 3; i++) {
            u[i] = colors[1][i] - colors[0][i];
            v[i] = colors[2][i] - colors[0][i];
            w[i] = p[i] - colors[0][i];
        }
        auto dot = [](const double *a, const double *b) {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        };
        double uu = dot(u, u), uv = dot(u, v), vv = dot(v, v), wu = dot(w, u), wv = dot(w, v);
        double det = uu * vv - uv * uv, s = (vv * wu - uv * wv) / det,
               t = (uu * wv - uv * wu) / det;
        if (s >= 0 && t >= 0 && s + t <= 1) {
            double d = 0;
            for (int i = 0; i < 3; i++)
                d += (w[i] - s * u[i] - t * v[i]) * (w[i] - s * u[i] - t * v[i]);
            return sqrt(d);
        }
        double best = 1e9;
        for (int a = 0; a < 3; a++)
            for (int b = a + 1; b < 3; b++)
                best = std::min(best, segment(p, colors[a], colors[b]));
        return best;
    };
    double worst = 0;
    for (size_t f = 8; f < frames.size(); f++)
        for (UINT i = 0; i < width * height; i++) {
            double p[3] = {(double)frames[f][i * 4 + 2], (double)frames[f][i * 4 + 1],
                           (double)frames[f][i * 4]};
            worst = std::max(worst, triangle(p));
        }
    return worst;
}
int main() {
    self = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = self;
    wc.lpszClassName = L"AlphaTemporalHiddenTest";
    RegisterClassW(&wc);
    HWND window = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 160, 120, nullptr,
                                nullptr, self, nullptr);
    IDirect3D9 *api = Direct3DCreate9(D3D_SDK_VERSION);
    assert(api);
    D3DDISPLAYMODE mode{};
    assert(SUCCEEDED(api->GetAdapterDisplayMode(0, &mode)));
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.hDeviceWindow = window;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth = 128;
    pp.BackBufferHeight = 96;
    pp.BackBufferFormat = mode.Format;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    HRESULT created = api->CreateDevice(0, D3DDEVTYPE_HAL, window,
                                        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(created)) {
        printf("GPU device unavailable: %08lx\n", created);
        return 2;
    }
    oldReset = (RESET)((*(void ***)dev)[16]);
    for (int cycle = 0; cycle < 12; cycle++) {
        useSMAA = (cycle % 4) >= 2;
        displaySoftness = cycle % 2 ? .5f : 0.f;
        assert(initialize());
        if (!useSMAA)
            assert(!edgesTexture && !weightsTexture && !spatialTexture && !areaLookup &&
                   !searchLookup);
        if (displaySoftness <= 0)
            assert(!softnessShader);
        assert(!valid);
        for (int f = 0; f < 3; f++) {
            assert(SUCCEEDED(dev->BeginScene()));
            for (int x = 0; x < 128; x++) {
                D3DRECT rect{x, 0, x + 1, 96};
                int value = x % 2 ? 180 : 70;
                assert(SUCCEEDED(dev->Clear(1, &rect, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                                            D3DCOLOR_XRGB(value, value, value), 1, 0)));
            }
            IDirect3DSurface9 *before = nullptr;
            dev->GetDepthStencilSurface(&before);
            DWORD nativeCull = D3DCULL_CW;
            dev->SetRenderState(D3DRS_CULLMODE, nativeCull);
            worldSeen = true;
            finished = false;
            jx = .25f;
            jy = 0;
            assert(apply());
            IDirect3DSurface9 *after = nullptr;
            dev->GetDepthStencilSurface(&after);
            assert(before == after);
            drop(before);
            drop(after);
            DWORD restored = 0;
            dev->GetRenderState(D3DRS_CULLMODE, &restored);
            assert(restored == nativeCull);
            D3DRECT hud{0, 0, 8, 8};
            assert(SUCCEEDED(dev->Clear(1, &hud, D3DCLEAR_TARGET, D3DCOLOR_XRGB(255, 0, 0), 1, 0)));
            assert(SUCCEEDED(dev->EndScene()));
            IDirect3DSurface9 *bb = nullptr, *cpu = nullptr;
            dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
            D3DSURFACE_DESC desc{};
            bb->GetDesc(&desc);
            assert(SUCCEEDED(dev->CreateOffscreenPlainSurface(width, height, desc.Format,
                                                              D3DPOOL_SYSTEMMEM, &cpu, nullptr)));
            assert(SUCCEEDED(dev->GetRenderTargetData(bb, cpu)));
            D3DLOCKED_RECT locked{};
            assert(SUCCEEDED(cpu->LockRect(&locked, nullptr, D3DLOCK_READONLY)));
            unsigned char *pixel = (unsigned char *)locked.pBits + 32 * locked.Pitch + 32 * 4;
            assert(pixel[0] > 70 && pixel[0] < 180);
            unsigned char *hudPixel = (unsigned char *)locked.pBits;
            assert(hudPixel[2] == 255 && hudPixel[0] == 0);
            int displayPixel = pixel[0];
            cpu->UnlockRect();
            if (displaySoftness > 0 && !useSMAA) {
                IDirect3DSurface9 *raw = nullptr;
                history->GetSurfaceLevel(0, &raw);
                assert(SUCCEEDED(dev->GetRenderTargetData(raw, cpu)));
                assert(SUCCEEDED(cpu->LockRect(&locked, nullptr, D3DLOCK_READONLY)));
                unsigned char *historyPixel =
                    (unsigned char *)locked.pBits + 32 * locked.Pitch + 32 * 4;
                assert(displayPixel > historyPixel[0] + 3);
                cpu->UnlockRect();
                drop(raw);
            }
            drop(cpu);
            drop(bb);
        }
        assert(SUCCEEDED(reset(dev, &pp)));
        assert(!ready && !scene && !history && !shader && !edgesTexture && !weightsTexture &&
               !spatialTexture && !areaLookup && !searchLookup && !softnessShader);
        for (auto *p : smaaShaders)
            assert(!p);
    }
    edgeStabilityTest(mode.Format);
    double shimmer = shimmerTest(mode.Format), fringe = fringeTest(mode.Format);
    printf("Static slanted edge, mean per-pixel range over 16 settled frames: %.2f\n", shimmer);
    printf("Panning three-color edge, worst distance from a real color mix: %.2f\n", fringe);
    fflush(stdout);
#ifndef BASELINE_RESOLVE
    assert(shimmer < 4 && fringe < 6);
#endif
    cleanup();
    drop(dev);
    drop(api);
    DestroyWindow(window);
    puts(
        "PASS: fused resolve blur, optional legacy display filters without feedback, no unused display resources; native depth/state, HUD exclusion, twelve device resets.");
    return 0;
}
