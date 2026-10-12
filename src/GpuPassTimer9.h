#pragma once
#include <d3d9.h>
#include <cstdio>

// Sparse GPU timestamps. Never flush, spin or wait for results; unsupported
// devices simply omit timing. Owned queries are released before every reset.
class GpuPassTimer9 {
    IDirect3DQuery9 *stamps[3]{}, *frequency = nullptr, *disjoint = nullptr;
    unsigned frames = 0;
    bool active = false, pending = false, disabled = false;
    template <class T> static void drop(T *&p) {
        if (p) {
            p->Release();
            p = nullptr;
        }
    }

  public:
    void reset() {
        for (auto &p : stamps)
            drop(p);
        drop(frequency);
        drop(disjoint);
        active = pending = disabled = false;
        frames = 0;
    }
    void begin(IDirect3DDevice9 *device, const char *label, void (*log)(const char *)) {
        ++frames;
        if (pending && frames % 4 == 0) {
            UINT64 time[3]{}, hz = 0;
            BOOL invalid = TRUE;
            bool ready = frequency->GetData(&hz, sizeof(hz), 0) == S_OK &&
                         disjoint->GetData(&invalid, sizeof(invalid), 0) == S_OK;
            for (int i = 0; i < 3 && ready; ++i)
                ready = stamps[i]->GetData(time + i, sizeof(time[i]), 0) == S_OK;
            if (ready) {
                if (!invalid && hz && time[0] <= time[1] && time[1] <= time[2]) {
                    char text[256];
                    sprintf_s(
                        text,
                        "GPU %s: first=%.3f ms second=%.3f ms total=%.3f ms (sparse, nonblocking)",
                        label, (time[1] - time[0]) * 1000.0 / hz, (time[2] - time[1]) * 1000.0 / hz,
                        (time[2] - time[0]) * 1000.0 / hz);
                    log(text);
                }
                pending = false;
            }
        }
        if (disabled || pending || frames % 120 != 1)
            return;
        if (!frequency) {
            bool ok = SUCCEEDED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &frequency)) &&
                      SUCCEEDED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &disjoint));
            for (auto &p : stamps)
                if (ok)
                    ok = SUCCEEDED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &p));
            if (!ok) {
                reset();
                disabled = true;
                return;
            }
        }
        active = SUCCEEDED(disjoint->Issue(D3DISSUE_BEGIN)) &&
                 SUCCEEDED(frequency->Issue(D3DISSUE_END)) &&
                 SUCCEEDED(stamps[0]->Issue(D3DISSUE_END));
    }
    void split() {
        if (active)
            stamps[1]->Issue(D3DISSUE_END);
    }
    void end() {
        if (!active)
            return;
        bool ok =
            SUCCEEDED(stamps[2]->Issue(D3DISSUE_END)) && SUCCEEDED(disjoint->Issue(D3DISSUE_END));
        pending = ok;
        active = false;
    }
};
