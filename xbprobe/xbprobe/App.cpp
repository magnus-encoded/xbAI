#include "pch.h"
#include "Probe.h"

using namespace Windows::ApplicationModel::Core;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::UI::Core;
using namespace Windows::Foundation;
using namespace Platform;

namespace
{
    // Map the probe status bits to a screen colour so a glance at the TV tells
    // the story before we even pull the log:
    //   blue    = T4 PASS: full HF file set downloaded on-device (or T2/T3 pass)
    //   amber   = T4: download in progress (D3D12 up, set not yet complete)
    //   red     = no D3D12 device
    // This is a T4 build (ORT + LAN probes skipped), so the HF bits drive the colour.
    void StatusColor(unsigned int s, float out[4])
    {
        out[3] = 1.0f;
        if (!(s & PROBE_D3D12_OK))            { out[0] = 0.70f; out[1] = 0.10f; out[2] = 0.10f; } // red:    no D3D12
        else if (s & PROBE_HF_COMPLETE)       { out[0] = 0.15f; out[1] = 0.45f; out[2] = 0.85f; } // blue:   T4 PASS (file set down)
        else if (s & PROBE_LAN_SERVED)        { out[0] = 0.15f; out[1] = 0.45f; out[2] = 0.85f; } // blue:   T3 PASS
        else if (s & PROBE_LAN_BOUND)         { out[0] = 0.65f; out[1] = 0.15f; out[2] = 0.75f; } // magenta:T3 bound
        else                                  { out[0] = 0.90f; out[1] = 0.60f; out[2] = 0.05f; } // amber:  T4 downloading / idle
    }
}

ref class ProbeView sealed : public IFrameworkView
{
public:
    virtual void Initialize(CoreApplicationView^ applicationView)
    {
        applicationView->Activated +=
            ref new TypedEventHandler<CoreApplicationView^, IActivatedEventArgs^>(this, &ProbeView::OnActivated);
    }

    virtual void SetWindow(CoreWindow^ window)
    {
        m_window = window;
        window->Closed +=
            ref new TypedEventHandler<CoreWindow^, CoreWindowEventArgs^>(this, &ProbeView::OnClosed);
    }

    virtual void Load(String^) {}

    virtual void Run()
    {
        // 1. Ask the hardware the real questions, persist the answers.
        unsigned int status = PROBE_NONE;
        std::wstring report = RunCapabilityProbe(status);
        // T4 build: on-device Hugging Face download spike — independent of the
        // inference path (T1c+T2 already PASSED). We SKIP the heavy ORT load and the
        // T3 listener and kick off the model file-set download on a ThreadPool worker
        // (NON-blocking: the 2 GB blob takes minutes; blocking the dispatcher that
        // long would trip the UWP activation watchdog). The worker live-updates the
        // report; here we just pump the window and poll for completion.
        report += L"\n[T4] launching on-device HF download on a background thread (live updates follow)\n";
        WriteReport(report);
        StartHfDownloadProbe(report);
        m_status = status;
        StatusColor(status, m_clear);

        // 2. Bring up a minimal D3D12 swapchain so the TV shows the status color.
        InitGraphics();

        // 3. Keep the window alive and repaint. Poll the T4 worker so the colour
        //    flips to "complete" the moment the full file set has landed.
        while (!m_windowClosed)
        {
            m_window->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
            if (!(m_status & PROBE_HF_COMPLETE) && HfDownloadComplete())
            {
                m_status |= PROBE_HF_COMPLETE;
                StatusColor(m_status, m_clear);
            }
            RenderFrame();
        }
    }

    virtual void Uninitialize() {}

private:
    void OnActivated(CoreApplicationView^, IActivatedEventArgs^)
    {
        CoreWindow::GetForCurrentThread()->Activate();
    }
    void OnClosed(CoreWindow^, CoreWindowEventArgs^) { m_windowClosed = true; }

    void WaitForGpu()
    {
        if (!m_queue || !m_fence) return;
        const UINT64 v = ++m_fenceValue;
        m_queue->Signal(m_fence.Get(), v);
        if (m_fence->GetCompletedValue() < v)
        {
            m_fence->SetEventOnCompletion(v, m_fenceEvent);
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
    }

    void InitGraphics()
    {
        ComPtr<IDXGIFactory4> factory;
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return;
        if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)))) return;

        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(m_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_queue)))) return;

        m_width = (UINT)(m_window->Bounds.Width);
        m_height = (UINT)(m_window->Bounds.Height);
        if (m_width == 0) m_width = 1280;
        if (m_height == 0) m_height = 720;

        DXGI_SWAP_CHAIN_DESC1 sc{};
        sc.BufferCount = kFrames;
        sc.Width = m_width;
        sc.Height = m_height;
        sc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sc.SampleDesc.Count = 1;

        ComPtr<IDXGISwapChain1> sc1;
        if (FAILED(factory->CreateSwapChainForCoreWindow(
                m_queue.Get(), reinterpret_cast<IUnknown*>(m_window), &sc, nullptr, &sc1)))
            return;
        sc1.As(&m_swapChain);

        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.NumDescriptors = kFrames;
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (FAILED(m_device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&m_rtvHeap)))) return;
        m_rtvSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        auto handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < kFrames; ++i)
        {
            m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_targets[i]));
            m_device->CreateRenderTargetView(m_targets[i].Get(), nullptr, handle);
            handle.ptr += m_rtvSize;
        }

        m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_alloc));
        m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc.Get(), nullptr, IID_PPV_ARGS(&m_list));
        m_list->Close();
        m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
        m_fenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        m_ready = true;
    }

    void RenderFrame()
    {
        if (!m_ready) return;
        UINT idx = m_swapChain->GetCurrentBackBufferIndex();

        m_alloc->Reset();
        m_list->Reset(m_alloc.Get(), nullptr);

        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = m_targets[idx].Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_list->ResourceBarrier(1, &b);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += (SIZE_T)idx * m_rtvSize;
        m_list->ClearRenderTargetView(rtv, m_clear, 0, nullptr);

        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        m_list->ResourceBarrier(1, &b);
        m_list->Close();

        ID3D12CommandList* lists[] = { m_list.Get() };
        m_queue->ExecuteCommandLists(1, lists);
        m_swapChain->Present(1, 0);
        WaitForGpu();
    }

    static const UINT kFrames = 2;

    CoreWindow^ m_window = nullptr;
    bool m_windowClosed = false;
    bool m_ready = false;
    unsigned int m_status = 0;
    float m_clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_queue;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12Resource> m_targets[kFrames];
    ComPtr<ID3D12CommandAllocator> m_alloc;
    ComPtr<ID3D12GraphicsCommandList> m_list;
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    UINT64 m_fenceValue = 0;
    UINT m_rtvSize = 0;
    UINT m_width = 0;
    UINT m_height = 0;
};

ref class ProbeViewSource sealed : IFrameworkViewSource
{
public:
    virtual IFrameworkView^ CreateView() { return ref new ProbeView(); }
};

[MTAThread]
int main(Array<String^>^)
{
    CoreApplication::Run(ref new ProbeViewSource());
    return 0;
}
