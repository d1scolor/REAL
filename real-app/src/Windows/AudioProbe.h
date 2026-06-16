#pragma once

#include "ComPtr.h"
#include "WindowsError.h"

#include <Audioclient.h>
#include <mmdeviceapi.h>
#include <tl/expected.hpp>

#include <string>
#include <vector>

namespace miniant::Windows::WasapiLatency {

enum class EndpointRole {
    Console,
    Multimedia,
    Communications
};

enum class StreamCategory {
    Other,
    Media,
    GameMedia,
    GameEffects,
    Communications,
    Movie,
    SoundEffects
};

enum class RawMode {
    Off,
    On,
    Auto
};

struct ProbeOptions {
    EndpointRole role = EndpointRole::Console;
    StreamCategory category = StreamCategory::Media;
    bool raw = false;
    bool matchFormat = false;
};

struct PeriodInfo {
    UINT32 defaultFrames = 0;
    UINT32 fundamentalFrames = 0;
    UINT32 minFrames = 0;
    UINT32 maxFrames = 0;
    UINT32 currentFrames = 0;
    UINT32 sampleRate = 0;
    double defaultMs = 0;
    double fundamentalMs = 0;
    double minMs = 0;
    double maxMs = 0;
    double currentMs = 0;
};

struct EndpointInfo {
    std::wstring id;
    std::wstring friendlyName;
    EDataFlow flow = eRender;
    DWORD state = 0;
};

struct ProbeResult {
    EndpointInfo endpoint;
    ProbeOptions options;
    PeriodInfo periods;
    HRESULT setPropertiesHr = E_FAIL;
    HRESULT mixFormatHr = E_FAIL;
    HRESULT queryPeriodsHr = E_FAIL;
    HRESULT initializeHr = E_FAIL;
    HRESULT currentPeriodHr = E_FAIL;
    bool canQueryPeriods = false;
    bool canInitialize = false;
    std::string notes;
};

struct EndpointSelection {
    ComPtr<IMMDevice> device;
    EndpointInfo info;
    EndpointRole role = EndpointRole::Console;
};

class ComApartment {
public:
    ComApartment();
    ~ComApartment();

    bool IsUsable() const noexcept;
    HRESULT Result() const noexcept;

private:
    HRESULT m_hr = E_FAIL;
    bool m_shouldUninitialize = false;
};

const char* ToString(EndpointRole role);
const char* ToString(StreamCategory category);
AUDIO_STREAM_CATEGORY ToAudioCategory(StreamCategory category);
ERole ToERole(EndpointRole role);

std::string WStringToUtf8(const std::wstring& value);
std::wstring Utf8ToWString(const std::string& value);

tl::expected<std::vector<EndpointSelection>, WindowsError> EnumerateRenderEndpoints(
    const std::vector<EndpointRole>& roles,
    bool includeAllActiveEndpoints);

std::vector<ProbeOptions> BuildProbeOptions(
    const std::vector<EndpointRole>& roles,
    const std::vector<StreamCategory>& categories,
    RawMode rawMode);

ProbeResult ProbeEndpoint(IMMDevice* device, const EndpointInfo& endpoint, const ProbeOptions& options);

std::vector<ProbeResult> ProbeEndpoints(
    const std::vector<EndpointSelection>& endpoints,
    const std::vector<StreamCategory>& categories,
    RawMode rawMode);

const ProbeResult* SelectBestProbe(const std::vector<ProbeResult>& results);
std::vector<const ProbeResult*> SelectBestProbes(const std::vector<ProbeResult>& results, size_t count);

std::string FormatProbeTable(const std::vector<ProbeResult>& results, bool verboseFailures);
std::string FormatProbeJson(const std::vector<ProbeResult>& results);
std::string FormatHRESULT(HRESULT hr);

}
