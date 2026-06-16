#include "AudioProbe.h"

#include <Functiondiscoverykeys_devpkey.h>
#include <PropIdl.h>
#include <Windows.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

#ifdef GetMessage
#undef GetMessage
#endif

using namespace miniant::Windows;
using namespace miniant::Windows::WasapiLatency;

namespace {

constexpr DWORD ACTIVE_ENDPOINT_STATE = DEVICE_STATE_ACTIVE;

double FramesToMs(UINT32 frames, UINT32 sampleRate) {
    if (sampleRate == 0) {
        return 0;
    }

    return 1000.0 * static_cast<double>(frames) / static_cast<double>(sampleRate);
}

std::string HResultHex(HRESULT hr) {
    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
        << static_cast<unsigned long>(hr);
    return oss.str();
}

std::string KnownHRESULTName(HRESULT hr) {
    switch (hr) {
    case S_OK: return "S_OK";
    case S_FALSE: return "S_FALSE";
    case E_POINTER: return "E_POINTER";
    case E_INVALIDARG: return "E_INVALIDARG";
    case E_OUTOFMEMORY: return "E_OUTOFMEMORY";
    case E_NOTIMPL: return "E_NOTIMPL";
    case E_NOINTERFACE: return "E_NOINTERFACE";
    case E_ACCESSDENIED: return "E_ACCESSDENIED";
#ifdef AUDCLNT_E_NOT_INITIALIZED
    case AUDCLNT_E_NOT_INITIALIZED: return "AUDCLNT_E_NOT_INITIALIZED";
#endif
#ifdef AUDCLNT_E_ALREADY_INITIALIZED
    case AUDCLNT_E_ALREADY_INITIALIZED: return "AUDCLNT_E_ALREADY_INITIALIZED";
#endif
#ifdef AUDCLNT_E_WRONG_ENDPOINT_TYPE
    case AUDCLNT_E_WRONG_ENDPOINT_TYPE: return "AUDCLNT_E_WRONG_ENDPOINT_TYPE";
#endif
#ifdef AUDCLNT_E_DEVICE_INVALIDATED
    case AUDCLNT_E_DEVICE_INVALIDATED: return "AUDCLNT_E_DEVICE_INVALIDATED";
#endif
#ifdef AUDCLNT_E_NOT_STOPPED
    case AUDCLNT_E_NOT_STOPPED: return "AUDCLNT_E_NOT_STOPPED";
#endif
#ifdef AUDCLNT_E_BUFFER_TOO_LARGE
    case AUDCLNT_E_BUFFER_TOO_LARGE: return "AUDCLNT_E_BUFFER_TOO_LARGE";
#endif
#ifdef AUDCLNT_E_OUT_OF_ORDER
    case AUDCLNT_E_OUT_OF_ORDER: return "AUDCLNT_E_OUT_OF_ORDER";
#endif
#ifdef AUDCLNT_E_UNSUPPORTED_FORMAT
    case AUDCLNT_E_UNSUPPORTED_FORMAT: return "AUDCLNT_E_UNSUPPORTED_FORMAT";
#endif
#ifdef AUDCLNT_E_INVALID_SIZE
    case AUDCLNT_E_INVALID_SIZE: return "AUDCLNT_E_INVALID_SIZE";
#endif
#ifdef AUDCLNT_E_DEVICE_IN_USE
    case AUDCLNT_E_DEVICE_IN_USE: return "AUDCLNT_E_DEVICE_IN_USE";
#endif
#ifdef AUDCLNT_E_SERVICE_NOT_RUNNING
    case AUDCLNT_E_SERVICE_NOT_RUNNING: return "AUDCLNT_E_SERVICE_NOT_RUNNING";
#endif
#ifdef AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED
    case AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED: return "AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED";
#endif
#ifdef AUDCLNT_E_EVENTHANDLE_NOT_SET
    case AUDCLNT_E_EVENTHANDLE_NOT_SET: return "AUDCLNT_E_EVENTHANDLE_NOT_SET";
#endif
#ifdef AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED
    case AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED: return "AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED";
#endif
#ifdef AUDCLNT_E_INVALID_DEVICE_PERIOD
    case AUDCLNT_E_INVALID_DEVICE_PERIOD: return "AUDCLNT_E_INVALID_DEVICE_PERIOD";
#endif
#ifdef AUDCLNT_E_ENGINE_PERIODICITY_LOCKED
    case AUDCLNT_E_ENGINE_PERIODICITY_LOCKED: return "AUDCLNT_E_ENGINE_PERIODICITY_LOCKED";
#endif
#ifdef AUDCLNT_E_ENGINE_FORMAT_LOCKED
    case AUDCLNT_E_ENGINE_FORMAT_LOCKED: return "AUDCLNT_E_ENGINE_FORMAT_LOCKED";
#endif
    default:
        return {};
    }
}

ComPtr<IMMDeviceEnumerator> CreateEnumerator(HRESULT& hr) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    hr = ::CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(enumerator.Put()));
    return enumerator;
}

std::wstring GetDeviceId(IMMDevice* device, HRESULT& hr) {
    LPWSTR id = nullptr;
    hr = device->GetId(&id);
    if (FAILED(hr)) {
        return {};
    }

    std::wstring result(id);
    ::CoTaskMemFree(id);
    return result;
}

std::wstring GetFriendlyName(IMMDevice* device) {
    ComPtr<IPropertyStore> store;
    HRESULT hr = device->OpenPropertyStore(STGM_READ, store.Put());
    if (FAILED(hr)) {
        return L"(unknown endpoint)";
    }

    PROPVARIANT value;
    ::PropVariantInit(&value);
    hr = store->GetValue(PKEY_Device_FriendlyName, &value);
    if (FAILED(hr) || value.vt != VT_LPWSTR || value.pwszVal == nullptr) {
        ::PropVariantClear(&value);
        return L"(unknown endpoint)";
    }

    std::wstring name(value.pwszVal);
    ::PropVariantClear(&value);
    return name;
}

tl::expected<EndpointInfo, WindowsError> ReadEndpointInfo(IMMDevice* device) {
    HRESULT hr = S_OK;

    EndpointInfo info;
    info.id = GetDeviceId(device, hr);
    if (FAILED(hr)) {
        return tl::make_unexpected(WindowsError::FromHRESULT(hr, "IMMDevice::GetId failed"));
    }

    info.friendlyName = GetFriendlyName(device);
    hr = device->GetState(&info.state);
    if (FAILED(hr)) {
        return tl::make_unexpected(WindowsError::FromHRESULT(hr, "IMMDevice::GetState failed"));
    }

    return info;
}

bool SameEndpoint(const std::wstring& lhs, const std::wstring& rhs) {
    return ::CompareStringOrdinal(lhs.c_str(), -1, rhs.c_str(), -1, TRUE) == CSTR_EQUAL;
}

void AddEndpointIfMissing(std::vector<EndpointSelection>& endpoints, EndpointSelection&& endpoint) {
    const bool exists = std::any_of(endpoints.begin(), endpoints.end(), [&](const EndpointSelection& existing) {
        return SameEndpoint(existing.info.id, endpoint.info.id);
        });

    if (!exists) {
        endpoints.emplace_back(std::move(endpoint));
    }
}

PeriodInfo BuildPeriodInfo(
    const WAVEFORMATEX* format,
    UINT32 defaultFrames,
    UINT32 fundamentalFrames,
    UINT32 minFrames,
    UINT32 maxFrames,
    UINT32 currentFrames) {
    PeriodInfo periods;
    periods.defaultFrames = defaultFrames;
    periods.fundamentalFrames = fundamentalFrames;
    periods.minFrames = minFrames;
    periods.maxFrames = maxFrames;
    periods.currentFrames = currentFrames;
    periods.sampleRate = format->nSamplesPerSec;
    periods.defaultMs = FramesToMs(defaultFrames, periods.sampleRate);
    periods.fundamentalMs = FramesToMs(fundamentalFrames, periods.sampleRate);
    periods.minMs = FramesToMs(minFrames, periods.sampleRate);
    periods.maxMs = FramesToMs(maxFrames, periods.sampleRate);
    periods.currentMs = FramesToMs(currentFrames, periods.sampleRate);
    return periods;
}

nlohmann::json PeriodsToJson(const PeriodInfo& periods) {
    return {
        { "defaultFrames", periods.defaultFrames },
        { "fundamentalFrames", periods.fundamentalFrames },
        { "minFrames", periods.minFrames },
        { "maxFrames", periods.maxFrames },
        { "currentFrames", periods.currentFrames },
        { "sampleRate", periods.sampleRate },
        { "defaultMs", periods.defaultMs },
        { "fundamentalMs", periods.fundamentalMs },
        { "minMs", periods.minMs },
        { "maxMs", periods.maxMs },
        { "currentMs", periods.currentMs }
    };
}

}

ComApartment::ComApartment() {
    m_hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (m_hr == S_OK || m_hr == S_FALSE) {
        m_shouldUninitialize = true;
    }
}

ComApartment::~ComApartment() {
    if (m_shouldUninitialize) {
        ::CoUninitialize();
    }
}

bool ComApartment::IsUsable() const noexcept {
    return SUCCEEDED(m_hr) || m_hr == RPC_E_CHANGED_MODE;
}

HRESULT ComApartment::Result() const noexcept {
    return m_hr;
}

const char* miniant::Windows::WasapiLatency::ToString(EndpointRole role) {
    switch (role) {
    case EndpointRole::Console: return "Console";
    case EndpointRole::Multimedia: return "Multimedia";
    case EndpointRole::Communications: return "Communications";
    default: return "Unknown";
    }
}

const char* miniant::Windows::WasapiLatency::ToString(StreamCategory category) {
    switch (category) {
    case StreamCategory::Other: return "Other";
    case StreamCategory::Media: return "Media";
    case StreamCategory::GameMedia: return "GameMedia";
    case StreamCategory::GameEffects: return "GameEffects";
    case StreamCategory::Communications: return "Communications";
    case StreamCategory::Movie: return "Movie";
    case StreamCategory::SoundEffects: return "SoundEffects";
    default: return "Unknown";
    }
}

AUDIO_STREAM_CATEGORY miniant::Windows::WasapiLatency::ToAudioCategory(StreamCategory category) {
    switch (category) {
    case StreamCategory::Other: return AudioCategory_Other;
    case StreamCategory::Media: return AudioCategory_Media;
    case StreamCategory::GameMedia: return AudioCategory_GameMedia;
    case StreamCategory::GameEffects: return AudioCategory_GameEffects;
    case StreamCategory::Communications: return AudioCategory_Communications;
    case StreamCategory::Movie: return AudioCategory_Movie;
    case StreamCategory::SoundEffects: return AudioCategory_SoundEffects;
    default: return AudioCategory_Other;
    }
}

ERole miniant::Windows::WasapiLatency::ToERole(EndpointRole role) {
    switch (role) {
    case EndpointRole::Console: return eConsole;
    case EndpointRole::Multimedia: return eMultimedia;
    case EndpointRole::Communications: return eCommunications;
    default: return eConsole;
    }
}

std::string miniant::Windows::WasapiLatency::WStringToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int size = ::WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }

    std::string result(static_cast<size_t>(size - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring miniant::Windows::WasapiLatency::Utf8ToWString(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int size = ::MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (size <= 1) {
        return {};
    }

    std::wstring result(static_cast<size_t>(size - 1), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), size);
    return result;
}

tl::expected<std::vector<EndpointSelection>, WindowsError> miniant::Windows::WasapiLatency::EnumerateRenderEndpoints(
    const std::vector<EndpointRole>& roles,
    bool includeAllActiveEndpoints) {
    HRESULT hr = S_OK;
    auto enumerator = CreateEnumerator(hr);
    if (FAILED(hr)) {
        return tl::make_unexpected(WindowsError::FromHRESULT(hr, "CoCreateInstance(MMDeviceEnumerator) failed"));
    }

    std::vector<EndpointSelection> endpoints;
    for (const auto role : roles) {
        ComPtr<IMMDevice> device;
        hr = enumerator->GetDefaultAudioEndpoint(eRender, ToERole(role), device.Put());
        if (FAILED(hr)) {
            continue;
        }

        auto info = ReadEndpointInfo(device.Get());
        if (!info) {
            return tl::make_unexpected(info.error());
        }

        EndpointSelection selection;
        selection.device = std::move(device);
        selection.info = *info;
        selection.info.flow = eRender;
        selection.role = role;
        AddEndpointIfMissing(endpoints, std::move(selection));
    }

    if (includeAllActiveEndpoints) {
        ComPtr<IMMDeviceCollection> collection;
        hr = enumerator->EnumAudioEndpoints(eRender, ACTIVE_ENDPOINT_STATE, collection.Put());
        if (FAILED(hr)) {
            return tl::make_unexpected(WindowsError::FromHRESULT(hr, "IMMDeviceEnumerator::EnumAudioEndpoints failed"));
        }

        UINT count = 0;
        hr = collection->GetCount(&count);
        if (FAILED(hr)) {
            return tl::make_unexpected(WindowsError::FromHRESULT(hr, "IMMDeviceCollection::GetCount failed"));
        }

        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> device;
            hr = collection->Item(i, device.Put());
            if (FAILED(hr)) {
                return tl::make_unexpected(WindowsError::FromHRESULT(hr, "IMMDeviceCollection::Item failed"));
            }

            auto info = ReadEndpointInfo(device.Get());
            if (!info) {
                return tl::make_unexpected(info.error());
            }

            EndpointSelection selection;
            selection.device = std::move(device);
            selection.info = *info;
            selection.info.flow = eRender;
            selection.role = EndpointRole::Console;
            AddEndpointIfMissing(endpoints, std::move(selection));
        }
    }

    if (endpoints.empty()) {
        return tl::make_unexpected(WindowsError("No active render endpoints were found."));
    }

    return endpoints;
}

std::vector<ProbeOptions> miniant::Windows::WasapiLatency::BuildProbeOptions(
    const std::vector<EndpointRole>& roles,
    const std::vector<StreamCategory>& categories,
    RawMode rawMode) {
    std::vector<bool> rawOptions;
    if (rawMode == RawMode::On || rawMode == RawMode::Auto) {
        rawOptions.push_back(true);
    }
    if (rawMode == RawMode::Off || rawMode == RawMode::Auto) {
        rawOptions.insert(rawOptions.begin(), false);
    }

    std::vector<ProbeOptions> options;
    for (const auto role : roles) {
        for (const auto category : categories) {
            for (const bool raw : rawOptions) {
                ProbeOptions option;
                option.role = role;
                option.category = category;
                option.raw = raw;
                options.push_back(option);
            }
        }
    }

    return options;
}

ProbeResult miniant::Windows::WasapiLatency::ProbeEndpoint(
    IMMDevice* device,
    const EndpointInfo& endpoint,
    const ProbeOptions& options) {
    ProbeResult result;
    result.endpoint = endpoint;
    result.options = options;

    ComPtr<IAudioClient3> audioClient;
    HRESULT hr = device->Activate(
        __uuidof(IAudioClient3),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(audioClient.Put()));
    if (FAILED(hr)) {
        result.notes = WindowsError::FromHRESULT(hr, "IMMDevice::Activate(IAudioClient3) failed").GetMessage();
        return result;
    }

    ComPtr<IAudioClient2> audioClient2;
    hr = audioClient->QueryInterface(__uuidof(IAudioClient2), reinterpret_cast<void**>(audioClient2.Put()));
    if (FAILED(hr)) {
        result.setPropertiesHr = hr;
        result.notes = WindowsError::FromHRESULT(hr, "IAudioClient3::QueryInterface(IAudioClient2) failed").GetMessage();
        return result;
    }

    AudioClientProperties properties = {};
    properties.cbSize = sizeof(properties);
    properties.eCategory = ToAudioCategory(options.category);
    if (options.raw) {
        properties.Options = static_cast<AUDCLNT_STREAMOPTIONS>(properties.Options | AUDCLNT_STREAMOPTIONS_RAW);
    }
#ifdef AUDCLNT_STREAMOPTIONS_MATCH_FORMAT
    if (options.matchFormat) {
        properties.Options = static_cast<AUDCLNT_STREAMOPTIONS>(properties.Options | AUDCLNT_STREAMOPTIONS_MATCH_FORMAT);
    }
#endif

    result.setPropertiesHr = audioClient2->SetClientProperties(&properties);
    if (FAILED(result.setPropertiesHr)) {
        result.notes = WindowsError::FromHRESULT(
            result.setPropertiesHr,
            options.raw ? "IAudioClient2::SetClientProperties(raw) failed" : "IAudioClient2::SetClientProperties failed")
            .GetMessage();
        return result;
    }

    WAVEFORMATEX* format = nullptr;
    result.mixFormatHr = audioClient->GetMixFormat(&format);
    if (FAILED(result.mixFormatHr)) {
        result.notes = WindowsError::FromHRESULT(result.mixFormatHr, "IAudioClient3::GetMixFormat failed").GetMessage();
        return result;
    }

    UINT32 defaultFrames = 0;
    UINT32 fundamentalFrames = 0;
    UINT32 minFrames = 0;
    UINT32 maxFrames = 0;
    result.queryPeriodsHr = audioClient->GetSharedModeEnginePeriod(
        format,
        &defaultFrames,
        &fundamentalFrames,
        &minFrames,
        &maxFrames);
    if (FAILED(result.queryPeriodsHr)) {
        result.notes = WindowsError::FromHRESULT(result.queryPeriodsHr, "IAudioClient3::GetSharedModeEnginePeriod failed").GetMessage();
        ::CoTaskMemFree(format);
        return result;
    }

    result.canQueryPeriods = true;
    result.periods = BuildPeriodInfo(format, defaultFrames, fundamentalFrames, minFrames, maxFrames, 0);

    result.initializeHr = audioClient->InitializeSharedAudioStream(
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        minFrames,
        format,
        nullptr);
    if (SUCCEEDED(result.initializeHr)) {
        result.canInitialize = true;

        WAVEFORMATEX* currentFormat = nullptr;
        UINT32 currentFrames = 0;
        result.currentPeriodHr = audioClient->GetCurrentSharedModeEnginePeriod(&currentFormat, &currentFrames);
        if (SUCCEEDED(result.currentPeriodHr)) {
            result.periods.currentFrames = currentFrames;
            result.periods.currentMs = FramesToMs(currentFrames, result.periods.sampleRate);
        }
        if (currentFormat != nullptr) {
            ::CoTaskMemFree(currentFormat);
        }
    } else {
        result.notes = WindowsError::FromHRESULT(result.initializeHr, "IAudioClient3::InitializeSharedAudioStream failed").GetMessage();
    }

    ::CoTaskMemFree(format);
    return result;
}

std::vector<ProbeResult> miniant::Windows::WasapiLatency::ProbeEndpoints(
    const std::vector<EndpointSelection>& endpoints,
    const std::vector<StreamCategory>& categories,
    RawMode rawMode) {
    ComApartment apartment;
    std::vector<ProbeResult> results;
    if (!apartment.IsUsable()) {
        ProbeResult result;
        result.notes = WindowsError::FromHRESULT(apartment.Result(), "CoInitializeEx failed").GetMessage();
        results.push_back(result);
        return results;
    }

    for (const auto& endpoint : endpoints) {
        std::vector<EndpointRole> roles = { endpoint.role };
        auto options = BuildProbeOptions(roles, categories, rawMode);
        for (auto option : options) {
            option.role = endpoint.role;
            results.push_back(ProbeEndpoint(endpoint.device.Get(), endpoint.info, option));
        }
    }

    return results;
}

const ProbeResult* miniant::Windows::WasapiLatency::SelectBestProbe(const std::vector<ProbeResult>& results) {
    auto selected = SelectBestProbes(results, 1);
    if (selected.empty()) {
        return nullptr;
    }

    return selected.front();
}

std::vector<const ProbeResult*> miniant::Windows::WasapiLatency::SelectBestProbes(
    const std::vector<ProbeResult>& results,
    size_t count) {
    std::vector<const ProbeResult*> candidates;
    for (const auto& result : results) {
        if (result.canInitialize) {
            candidates.push_back(&result);
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const ProbeResult* lhs, const ProbeResult* rhs) {
        if (lhs->periods.minMs != rhs->periods.minMs) {
            return lhs->periods.minMs < rhs->periods.minMs;
        }
        if (lhs->periods.currentMs != rhs->periods.currentMs) {
            return lhs->periods.currentMs < rhs->periods.currentMs;
        }
        if (lhs->options.raw != rhs->options.raw) {
            return !lhs->options.raw;
        }
        return std::string(ToString(lhs->options.category)) < std::string(ToString(rhs->options.category));
        });

    if (candidates.size() > count) {
        candidates.resize(count);
    }

    return candidates;
}

std::string miniant::Windows::WasapiLatency::FormatHRESULT(HRESULT hr) {
    auto name = KnownHRESULTName(hr);
    if (name.empty()) {
        return HResultHex(hr);
    }

    return HResultHex(hr) + " " + name;
}

std::string miniant::Windows::WasapiLatency::FormatProbeTable(
    const std::vector<ProbeResult>& results,
    bool verboseFailures) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    for (const auto& result : results) {
        oss << "Endpoint: " << WStringToUtf8(result.endpoint.friendlyName) << "\n";
        oss << "Role: " << ToString(result.options.role) << "\n";
        oss << "Category: " << ToString(result.options.category) << "\n";
        oss << "Raw: " << (result.options.raw ? "true" : "false") << "\n";

        if (result.canQueryPeriods) {
            oss << "Default: " << result.periods.defaultFrames << " frames / " << result.periods.defaultMs << " ms\n";
            oss << "Min: " << result.periods.minFrames << " frames / " << result.periods.minMs << " ms\n";
            oss << "Max: " << result.periods.maxFrames << " frames / " << result.periods.maxMs << " ms\n";
        }

        oss << "Set properties: " << FormatHRESULT(result.setPropertiesHr) << "\n";
        if (result.mixFormatHr != E_FAIL || result.canQueryPeriods) {
            oss << "Mix format: " << FormatHRESULT(result.mixFormatHr) << "\n";
        }
        if (result.queryPeriodsHr != E_FAIL || result.canQueryPeriods) {
            oss << "Query periods: " << FormatHRESULT(result.queryPeriodsHr) << "\n";
        }
        oss << "Initialize: " << FormatHRESULT(result.initializeHr) << "\n";
        if (result.currentPeriodHr != E_FAIL) {
            oss << "Current: " << result.periods.currentFrames << " frames / " << result.periods.currentMs
                << " ms (" << FormatHRESULT(result.currentPeriodHr) << ")\n";
        }
        if (verboseFailures && !result.notes.empty()) {
            oss << "Notes: " << result.notes << "\n";
        }

        oss << "\n";
    }

    return oss.str();
}

std::string miniant::Windows::WasapiLatency::FormatProbeJson(const std::vector<ProbeResult>& results) {
    nlohmann::json root = nlohmann::json::array();

    for (const auto& result : results) {
        root.push_back({
            { "endpoint", {
                { "id", WStringToUtf8(result.endpoint.id) },
                { "friendlyName", WStringToUtf8(result.endpoint.friendlyName) },
                { "state", result.endpoint.state }
            } },
            { "options", {
                { "role", ToString(result.options.role) },
                { "category", ToString(result.options.category) },
                { "raw", result.options.raw },
                { "matchFormat", result.options.matchFormat }
            } },
            { "periods", PeriodsToJson(result.periods) },
            { "setPropertiesHr", FormatHRESULT(result.setPropertiesHr) },
            { "mixFormatHr", FormatHRESULT(result.mixFormatHr) },
            { "queryPeriodsHr", FormatHRESULT(result.queryPeriodsHr) },
            { "initializeHr", FormatHRESULT(result.initializeHr) },
            { "currentPeriodHr", FormatHRESULT(result.currentPeriodHr) },
            { "canQueryPeriods", result.canQueryPeriods },
            { "canInitialize", result.canInitialize },
            { "notes", result.notes }
        });
    }

    return root.dump(2);
}
