#pragma once

#include "AudioProbe.h"
#include "ComPtr.h"
#include "WindowsError.h"

#include <Audioclient.h>
#include <tl/expected.hpp>

#include <atomic>
#include <memory>
#include <thread>

namespace miniant::Windows::WasapiLatency {

class LowLatencyRenderStream {
public:
    LowLatencyRenderStream() = default;
    LowLatencyRenderStream(const LowLatencyRenderStream&) = delete;
    LowLatencyRenderStream& operator=(const LowLatencyRenderStream&) = delete;
    LowLatencyRenderStream(LowLatencyRenderStream&& other) noexcept;
    LowLatencyRenderStream& operator=(LowLatencyRenderStream&& other) noexcept;
    ~LowLatencyRenderStream();

    static tl::expected<std::unique_ptr<LowLatencyRenderStream>, WindowsError> Start(IMMDevice* device, const ProbeResult& probe);

    void Stop();
    bool IsRunning() const noexcept;
    const ProbeResult& Probe() const noexcept;

private:
    ComPtr<IAudioClient3> m_audioClient;
    ComPtr<IAudioRenderClient> m_renderClient;
    WAVEFORMATEX* m_format = nullptr;
    HANDLE m_renderEvent = nullptr;
    HANDLE m_stopEvent = nullptr;
    UINT32 m_bufferFrames = 0;
    ProbeResult m_probe;
    std::thread m_worker;
    std::atomic<bool> m_running = false;

    void WorkerLoop();
    void MoveFrom(LowLatencyRenderStream&& other) noexcept;
    void ReleaseResources();
};

}
