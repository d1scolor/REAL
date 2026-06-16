#include "LowLatencyRenderStream.h"

#include <Avrt.h>
#include <Windows.h>

#include <algorithm>

using namespace miniant::Windows;
using namespace miniant::Windows::WasapiLatency;

namespace {

HANDLE CreateManualResetEvent() {
    return ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
}

HANDLE CreateAutoResetEvent() {
    return ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

}

LowLatencyRenderStream::LowLatencyRenderStream(LowLatencyRenderStream&& other) noexcept {
    MoveFrom(std::move(other));
}

LowLatencyRenderStream& LowLatencyRenderStream::operator=(LowLatencyRenderStream&& other) noexcept {
    if (this != &other) {
        Stop();
        ReleaseResources();
        MoveFrom(std::move(other));
    }

    return *this;
}

LowLatencyRenderStream::~LowLatencyRenderStream() {
    Stop();
    ReleaseResources();
}

tl::expected<std::unique_ptr<LowLatencyRenderStream>, WindowsError> LowLatencyRenderStream::Start(IMMDevice* device, const ProbeResult& probe) {
    auto stream = std::make_unique<LowLatencyRenderStream>();
    stream->m_probe = probe;

    HRESULT hr = device->Activate(
        __uuidof(IAudioClient3),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(stream->m_audioClient.Put()));
    if (FAILED(hr)) {
        return tl::make_unexpected(WindowsError::FromHRESULT(hr, "IMMDevice::Activate(IAudioClient3) failed"));
    }

    ComPtr<IAudioClient2> audioClient2;
    hr = stream->m_audioClient->QueryInterface(__uuidof(IAudioClient2), reinterpret_cast<void**>(audioClient2.Put()));
    if (FAILED(hr)) {
        return tl::make_unexpected(WindowsError::FromHRESULT(hr, "IAudioClient3::QueryInterface(IAudioClient2) failed"));
    }

    AudioClientProperties properties = {};
    properties.cbSize = sizeof(properties);
    properties.eCategory = ToAudioCategory(probe.options.category);
    if (probe.options.raw) {
        properties.Options = static_cast<AUDCLNT_STREAMOPTIONS>(properties.Options | AUDCLNT_STREAMOPTIONS_RAW);
    }
#ifdef AUDCLNT_STREAMOPTIONS_MATCH_FORMAT
    if (probe.options.matchFormat) {
        properties.Options = static_cast<AUDCLNT_STREAMOPTIONS>(properties.Options | AUDCLNT_STREAMOPTIONS_MATCH_FORMAT);
    }
#endif

    hr = audioClient2->SetClientProperties(&properties);
    if (FAILED(hr)) {
        return tl::make_unexpected(WindowsError::FromHRESULT(hr, "IAudioClient2::SetClientProperties failed"));
    }

    hr = stream->m_audioClient->GetMixFormat(&stream->m_format);
    if (FAILED(hr)) {
        return tl::make_unexpected(WindowsError::FromHRESULT(hr, "IAudioClient3::GetMixFormat failed"));
    }

    UINT32 defaultFrames = 0;
    UINT32 fundamentalFrames = 0;
    UINT32 minFrames = 0;
    UINT32 maxFrames = 0;
    hr = stream->m_audioClient->GetSharedModeEnginePeriod(
        stream->m_format,
        &defaultFrames,
        &fundamentalFrames,
        &minFrames,
        &maxFrames);
    if (FAILED(hr)) {
        return tl::make_unexpected(WindowsError::FromHRESULT(hr, "IAudioClient3::GetSharedModeEnginePeriod failed"));
    }

    hr = stream->m_audioClient->InitializeSharedAudioStream(
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        probe.periods.minFrames != 0 ? probe.periods.minFrames : minFrames,
        stream->m_format,
        nullptr);
    if (FAILED(hr)) {
        return tl::make_unexpected(WindowsError::FromHRESULT(hr, "IAudioClient3::InitializeSharedAudioStream failed"));
    }

    stream->m_renderEvent = CreateAutoResetEvent();
    stream->m_stopEvent = CreateManualResetEvent();
    if (stream->m_renderEvent == nullptr || stream->m_stopEvent == nullptr) {
        return tl::make_unexpected(WindowsError::FromLastError("CreateEvent failed"));
    }

    hr = stream->m_audioClient->SetEventHandle(stream->m_renderEvent);
    if (FAILED(hr)) {
        return tl::make_unexpected(WindowsError::FromHRESULT(hr, "IAudioClient::SetEventHandle failed"));
    }

    hr = stream->m_audioClient->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(stream->m_renderClient.Put()));
    if (FAILED(hr)) {
        return tl::make_unexpected(WindowsError::FromHRESULT(hr, "IAudioClient::GetService(IAudioRenderClient) failed"));
    }

    hr = stream->m_audioClient->GetBufferSize(&stream->m_bufferFrames);
    if (FAILED(hr)) {
        return tl::make_unexpected(WindowsError::FromHRESULT(hr, "IAudioClient::GetBufferSize failed"));
    }

    BYTE* buffer = nullptr;
    hr = stream->m_renderClient->GetBuffer(stream->m_bufferFrames, &buffer);
    if (FAILED(hr)) {
        return tl::make_unexpected(WindowsError::FromHRESULT(hr, "IAudioRenderClient::GetBuffer failed"));
    }

    hr = stream->m_renderClient->ReleaseBuffer(stream->m_bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);
    if (FAILED(hr)) {
        return tl::make_unexpected(WindowsError::FromHRESULT(hr, "IAudioRenderClient::ReleaseBuffer failed"));
    }

    hr = stream->m_audioClient->Start();
    if (FAILED(hr)) {
        return tl::make_unexpected(WindowsError::FromHRESULT(hr, "IAudioClient::Start failed"));
    }

    stream->m_running = true;
    LowLatencyRenderStream* streamPtr = stream.get();
    stream->m_worker = std::thread([streamPtr] {
        streamPtr->WorkerLoop();
        });

    return stream;
}

void LowLatencyRenderStream::Stop() {
    if (!m_running) {
        return;
    }

    m_running = false;
    if (m_stopEvent != nullptr) {
        ::SetEvent(m_stopEvent);
    }

    if (m_worker.joinable()) {
        m_worker.join();
    }

    if (m_audioClient) {
        m_audioClient->Stop();
    }
}

bool LowLatencyRenderStream::IsRunning() const noexcept {
    return m_running;
}

const ProbeResult& LowLatencyRenderStream::Probe() const noexcept {
    return m_probe;
}

void LowLatencyRenderStream::WorkerLoop() {
    ComApartment apartment;

    DWORD avrtTaskIndex = 0;
    const wchar_t* taskName = m_probe.periods.minMs < 10.0 ? L"Pro Audio" : L"Audio";
    HANDLE avrtHandle = ::AvSetMmThreadCharacteristicsW(taskName, &avrtTaskIndex);

    HANDLE handles[] = { m_stopEvent, m_renderEvent };

    while (m_running) {
        DWORD waitResult = ::WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (waitResult == WAIT_OBJECT_0) {
            break;
        }
        if (waitResult != WAIT_OBJECT_0 + 1) {
            break;
        }

        UINT32 paddingFrames = 0;
        HRESULT hr = m_audioClient->GetCurrentPadding(&paddingFrames);
        if (FAILED(hr)) {
            break;
        }

        const UINT32 availableFrames = m_bufferFrames > paddingFrames ? m_bufferFrames - paddingFrames : 0;
        if (availableFrames == 0) {
            continue;
        }

        BYTE* buffer = nullptr;
        hr = m_renderClient->GetBuffer(availableFrames, &buffer);
        if (FAILED(hr)) {
            break;
        }

        hr = m_renderClient->ReleaseBuffer(availableFrames, AUDCLNT_BUFFERFLAGS_SILENT);
        if (FAILED(hr)) {
            break;
        }
    }

    if (avrtHandle != nullptr) {
        ::AvRevertMmThreadCharacteristics(avrtHandle);
    }
}

void LowLatencyRenderStream::MoveFrom(LowLatencyRenderStream&& other) noexcept {
    m_audioClient = std::move(other.m_audioClient);
    m_renderClient = std::move(other.m_renderClient);
    m_format = other.m_format;
    m_renderEvent = other.m_renderEvent;
    m_stopEvent = other.m_stopEvent;
    m_bufferFrames = other.m_bufferFrames;
    m_probe = std::move(other.m_probe);
    m_running = other.m_running.load();
    m_worker = std::move(other.m_worker);

    other.m_format = nullptr;
    other.m_renderEvent = nullptr;
    other.m_stopEvent = nullptr;
    other.m_bufferFrames = 0;
    other.m_running = false;
}

void LowLatencyRenderStream::ReleaseResources() {
    if (m_format != nullptr) {
        ::CoTaskMemFree(m_format);
        m_format = nullptr;
    }

    if (m_renderEvent != nullptr) {
        ::CloseHandle(m_renderEvent);
        m_renderEvent = nullptr;
    }

    if (m_stopEvent != nullptr) {
        ::CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }

    m_renderClient.Reset();
    m_audioClient.Reset();
}
