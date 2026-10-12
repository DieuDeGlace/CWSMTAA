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
    cleanup();
    drop(dev);
    drop(api);
    DestroyWindow(window);
    puts(
        "PASS: fused resolve blur, optional legacy display filters without feedback, no unused display resources; native depth/state, HUD exclusion, twelve device resets.");
    return 0;
}
