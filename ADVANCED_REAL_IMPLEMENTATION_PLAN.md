# Advanced REAL Implementation Plan

This document is a handoff plan for continuing exploratory work on REAL, a
Windows audio latency keepalive utility. It summarizes the repository context,
the driver behavior analysis, and a practical implementation path for building a
more capable version of the app.

## Goal

Build a more advanced Windows-only REAL app that can:

- Diagnose what latency periods each installed render endpoint and driver path
  actually advertises.
- Try supported WASAPI stream properties, categories, and raw processing modes.
- Keep alive the best valid low-latency shared-mode stream or streams.
- Explain clearly when a driver is hard-limited to 10 ms from user mode.
- Produce enough diagnostic output to guide future work on affected Realtek/OEM
  systems.

The best implementation path is diagnostic-first. Do not start with kernel
drivers, INF overrides, registry edits, or driver patching. First prove what the
installed Windows audio stack exposes through documented APIs.

## Current Repository Context

The repo is small. The latency behavior is concentrated in:

- `real-app/src/Windows/MinimumLatencyAudioClient.cpp`
- `real-app/src/Windows/MinimumLatencyAudioClient.h`
- `real-app/src/main.cpp`
- `README.md`

Current behavior in `MinimumLatencyAudioClient::Start()`:

1. Calls `CoInitialize`.
2. Creates `IMMDeviceEnumerator`.
3. Opens the default render endpoint with
   `GetDefaultAudioEndpoint(eRender, eConsole)`.
4. Activates `IAudioClient3`.
5. Calls `GetMixFormat`.
6. Calls `GetSharedModeEnginePeriod`.
7. Calls `InitializeSharedAudioStream(0, minPeriodInFrames, pFormat, NULL)`.
8. Calls `Start` and keeps the `IAudioClient3` alive.

The current app therefore can only request the minimum shared-mode period that
the active driver path advertises. It does not force hardware, firmware, driver,
or registry state.

The README recommends installing Microsoft's generic "High Definition Audio
Device" driver because that inbox HDAudio path often advertises smaller shared
mode periods than OEM Realtek drivers.

## Why Newer Realtek/OEM Drivers Often Stay At 10 ms

The important distinction is between the codec and the whole driver path. A
machine may use a Realtek audio codec while the Windows endpoint is controlled
through an OEM-specific stack involving Realtek APOs, Intel SST, AMD ACP,
SoundWire, I2S, DSP firmware, speaker protection, or other enhancement layers.

Possible reasons the current app mostly works with the generic driver:

- The Microsoft inbox HDAudio stack often exposes smaller shared-mode minimum
  periods.
- OEM Realtek drivers may advertise 10 ms as the minimum period because their
  APO, DSP, speaker-protection, or bus path is only validated at that period.
- Newer systems may not allow the generic HDAudio driver to bind cleanly because
  the endpoint is not exposed as a simple HDAudio function device.
- The current app does not call `IAudioClient2::SetClientProperties`, so it only
  tests one implicit stream mode.
- On Windows 10 and later, audio categories map to driver-defined signal
  processing modes, and different modes can have different period constraints.

If a driver reports 10 ms as the minimum for every endpoint, role, category,
format, and raw/non-raw path, a normal user-mode app cannot force shared-mode
Windows audio below that value.

## Relevant Windows Audio Concepts

Use the following concepts when implementing and debugging:

- `IAudioClient3::GetSharedModeEnginePeriod` returns the default, fundamental,
  minimum, and maximum shared-mode engine periods for a stream format and
  previously set client properties.
- `IAudioClient3::InitializeSharedAudioStream` requests a shared-mode stream at
  a supported period.
- `IAudioClient2::SetClientProperties` should be called before querying periods
  or initializing a stream. Its category and options can affect the driver mode
  selected by Windows.
- `AUDCLNT_STREAMOPTIONS_RAW` requests raw processing when the endpoint supports
  it. Raw mode bypasses OEM-selected signal processing and may reduce latency,
  but can also change sound quality or speaker tuning.
- Drivers declare low-latency capabilities through driver packet-size
  constraints, including mode-specific constraints.
- APOs are user-mode audio processing objects installed by OEMs or IHVs. Realtek,
  Nahimic, Sonic Studio, DTS, Dolby, Waves, and MaxxAudio style components can
  affect the selected path.
- WASAPI exclusive mode, ASIO, and Kernel Streaming can bypass parts of the
  shared-mode engine for apps that use those APIs, but they do not globally lower
  shared-mode latency for normal apps.

Useful Microsoft documentation:

- https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio
- https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient3-getsharedmodeengineperiod
- https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient3-initializesharedaudiostream
- https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient2-setclientproperties
- https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/audio-signal-processing-modes
- https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/audio-processing-object-architecture
- https://learn.microsoft.com/en-us/windows/win32/coreaudio/exclusive-mode-streams
- https://learn.microsoft.com/en-us/windows/win32/coreaudio/device-formats
- https://learn.microsoft.com/en-us/windows-hardware/drivers/install/using-an-extension-inf-file
- https://learn.microsoft.com/en-us/windows-hardware/drivers/install/driver-signing

## Build And Test Environment

This app should be built and tested on Windows. The source can be edited on macOS,
but the binary depends on Win32, COM, WASAPI, Windows SDK headers, resources, and
actual Windows audio driver behavior.

Recommended environment:

- Windows 10 or Windows 11.
- Visual Studio 2022 or Visual Studio Build Tools.
- Windows 10/11 SDK.
- CMake.
- Real audio hardware for meaningful latency results. A VM can build the app, but
  likely will not reproduce Realtek/OEM driver behavior unless audio passthrough
  is unusually good.

## Phase 1: Modernize Conservatively

Keep the project shape close to the existing codebase.

- Keep C++17.
- Avoid broad dependency churn.
- Add Windows libraries explicitly if required:
  - `ole32`
  - `uuid`
  - `propsys`
  - `avrt`
- Preserve the existing console and tray behavior initially.
- Do not introduce a UI rewrite until the probe and keepalive engine is correct.

## Phase 2: Fix HRESULT Error Reporting

Current `WindowsError` uses `GetLastError`, which is usually wrong for COM and
WASAPI failures. Add HRESULT-aware error handling before making behavior changes.

Add helpers such as:

```cpp
class WindowsError : public ExpectedError {
public:
    static WindowsError FromHRESULT(HRESULT hr, const std::string& context);
};
```

The output should include:

- Call-site context.
- Hex HRESULT.
- Known symbolic names for common WASAPI errors when possible.
- Formatted system message when available.

Examples:

```text
IAudioClient3::InitializeSharedAudioStream failed: 0x88890019 AUDCLNT_E_ENGINE_PERIODICITY_LOCKED
IAudioClient2::SetClientProperties(raw) failed: 0x88890008 AUDCLNT_E_UNSUPPORTED_FORMAT
```

This is necessary because newer drivers can fail for different reasons:

- Unsupported raw mode.
- Engine format already locked.
- Engine periodicity already locked.
- Device in use.
- Unsupported format.
- Endpoint invalidated.

## Phase 3: Introduce Probe Data Types

Add a new probe layer, for example:

- `real-app/src/Windows/AudioProbe.h`
- `real-app/src/Windows/AudioProbe.cpp`
- `real-app/src/Windows/ComPtr.h` if a small local COM RAII wrapper is useful.

Suggested data model:

```cpp
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

struct ProbeOptions {
    EndpointRole role;
    StreamCategory category;
    bool raw;
    bool matchFormat;
};

struct PeriodInfo {
    UINT32 defaultFrames;
    UINT32 fundamentalFrames;
    UINT32 minFrames;
    UINT32 maxFrames;
    UINT32 currentFrames;
    UINT32 sampleRate;
    double defaultMs;
    double minMs;
    double maxMs;
    double currentMs;
};

struct EndpointInfo {
    std::wstring id;
    std::wstring friendlyName;
    EDataFlow flow;
    DWORD state;
};

struct ProbeResult {
    EndpointInfo endpoint;
    ProbeOptions options;
    PeriodInfo periods;
    HRESULT setPropertiesHr;
    HRESULT queryPeriodsHr;
    HRESULT initializeHr;
    bool canQueryPeriods;
    bool canInitialize;
    std::string notes;
};
```

Use RAII for:

- COM interfaces.
- `WAVEFORMATEX*` returned by `GetMixFormat`.
- Event handles.
- Threads.

## Phase 4: Enumerate Endpoints And Roles

Current REAL only opens `eRender/eConsole`.

Add enumeration for:

- Default render endpoint for `eConsole`.
- Default render endpoint for `eMultimedia`.
- Default render endpoint for `eCommunications`.
- Optionally all active render endpoints.

Relevant APIs:

- `IMMDeviceEnumerator::GetDefaultAudioEndpoint`
- `IMMDeviceEnumerator::EnumAudioEndpoints`
- `IMMDeviceCollection`
- `IMMDevice::GetId`
- `IMMDevice::OpenPropertyStore`
- `IPropertyStore::GetValue`
- `PKEY_Device_FriendlyName`

Add command-line options:

```text
--all-endpoints
--roles all|console|multimedia|communications
```

Default behavior should probe the three default render roles and deduplicate
endpoints by endpoint ID.

## Phase 5: Probe Categories And Raw Mode

For each endpoint/role combination, test multiple stream properties.

Categories to test first:

- `AudioCategory_Media`
- `AudioCategory_GameMedia`
- `AudioCategory_GameEffects`
- `AudioCategory_Communications`
- `AudioCategory_Other`

Optional categories:

- `AudioCategory_Movie`
- `AudioCategory_SoundEffects`

Raw modes:

- `raw=false`
- `raw=true` using `AUDCLNT_STREAMOPTIONS_RAW`

Important implementation details:

- Activate a fresh `IAudioClient3` for each probe combination.
- Query `IAudioClient2`.
- Call `SetClientProperties` before `GetSharedModeEnginePeriod`.
- Then call `GetMixFormat`.
- Then call `GetSharedModeEnginePeriod`.
- Then try `InitializeSharedAudioStream`.
- If initialization succeeds, call `GetCurrentSharedModeEnginePeriod` to verify
  the actual current engine period.

Pseudocode:

```cpp
ProbeResult ProbeEndpoint(IMMDevice* device, const ProbeOptions& options) {
    auto audioClient = Activate<IAudioClient3>(device);
    auto audioClient2 = Query<IAudioClient2>(audioClient);

    AudioClientProperties props = {};
    props.cbSize = sizeof(props);
    props.eCategory = ToAudioCategory(options.category);
    if (options.raw) {
        props.Options |= AUDCLNT_STREAMOPTIONS_RAW;
    }
    if (options.matchFormat) {
        props.Options |= AUDCLNT_STREAMOPTIONS_MATCH_FORMAT;
    }

    hr = audioClient2->SetClientProperties(&props);
    if (FAILED(hr)) {
        record and return;
    }

    hr = audioClient->GetMixFormat(&format);
    if (FAILED(hr)) {
        record and return;
    }

    hr = audioClient->GetSharedModeEnginePeriod(
        format,
        &defaultFrames,
        &fundamentalFrames,
        &minFrames,
        &maxFrames);
    if (FAILED(hr)) {
        record and return;
    }

    hr = audioClient->InitializeSharedAudioStream(
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        minFrames,
        format,
        nullptr);

    if (SUCCEEDED(hr)) {
        audioClient->GetCurrentSharedModeEnginePeriod(...);
    }
}
```

Raw mode failure is expected on some endpoints. Log it as unsupported rather than
treating it as an app failure.

## Phase 6: Optional Format Probing

Keep this behind a diagnostic flag because it can produce many combinations.

Suggested option:

```text
--probe-formats
```

Formats to test:

- Current mix format.
- 48 kHz stereo 16-bit PCM.
- 48 kHz stereo 24-bit PCM.
- 48 kHz stereo 32-bit float.
- 44.1 kHz stereo 16-bit PCM.
- 44.1 kHz stereo 24-bit PCM.
- 44.1 kHz stereo 32-bit float.

Use:

- `IAudioClient::IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, ...)`
- `IAudioClient3::GetSharedModeEnginePeriod(...)`

Do not assume this will beat the driver limit. It is mainly useful for detecting
drivers whose period tables vary by mix format.

## Phase 7: Select The Best Keepalive Stream

Selection algorithm:

1. Only consider probes that can initialize successfully.
2. Prefer the lowest `minMs`.
3. Prefer lower verified `currentMs` when available.
4. Prefer non-raw over raw if latency is equal, because raw may bypass OEM tuning.
5. Prefer user-selected roles/categories when provided.
6. If multiple categories or roles expose distinct low periods, optionally keep
   more than one stream alive.

Command-line options:

```text
--probe
--json
--keep-best N
--threshold-ms 10
--categories all|media|game|communications|other
--raw auto|on|off
```

Default behavior:

- Probe default render roles.
- Try common categories with raw auto.
- Keep the best stream alive.
- Print whether latency was actually improved below the threshold.

## Phase 8: Implement A Correct Silent Render Keepalive

The current code starts a stream but does not service render buffers. A more
correct implementation should render silence with an event-driven WASAPI loop.

Create a `LowLatencyRenderStream` class:

Responsibilities:

- Own `IAudioClient3`.
- Own `IAudioRenderClient`.
- Own the selected `WAVEFORMATEX`.
- Own an event handle.
- Own a worker thread.
- Start/stop cleanly.
- Fill available buffers with silence.

Initialization:

1. Activate and configure `IAudioClient3`.
2. Call `SetClientProperties`.
3. Call `InitializeSharedAudioStream` with
   `AUDCLNT_STREAMFLAGS_EVENTCALLBACK`.
4. Call `SetEventHandle`.
5. Query `IAudioRenderClient`.
6. Prime the initial buffer with silence.
7. Start the audio client.
8. Start worker loop.

Worker loop:

1. Use `AvSetMmThreadCharacteristics`.
   - Use `"Pro Audio"` for periods below 10 ms.
   - Use `"Audio"` otherwise.
2. Wait for the render event.
3. Call `GetCurrentPadding`.
4. Compute available frames.
5. Call `IAudioRenderClient::GetBuffer`.
6. Call `ReleaseBuffer(..., AUDCLNT_BUFFERFLAGS_SILENT)`.
7. Exit cleanly on stop event.

This makes the keepalive stream more legitimate and reduces the chance that
newer drivers or the audio engine ignore an unserviced stream.

## Phase 9: Output Shape

Replace optimistic output with accurate reporting.

Good result example:

```text
Selected endpoint: Speakers (Realtek(R) Audio)
Role: Console
Category: GameEffects
Raw mode: enabled
Driver minimum: 128 frames / 2.67 ms
Default period: 480 frames / 10.00 ms
Current engine period: 128 frames / 2.67 ms
Keepalive: active
```

No-improvement example:

```text
No shared-mode path below 10.00 ms was found.
The installed driver reports 10.00 ms as its minimum for all tested categories,
roles, and raw/non-raw modes.

REAL cannot force a lower shared-mode period from user mode.
Try disabling OEM audio effects, using WASAPI exclusive or ASIO in target apps,
or testing another driver package.
```

Diagnostics should show enough detail to compare systems:

```text
Endpoint: Speakers (Realtek(R) Audio)
Role: Console
Category: Media
Raw: false
Default: 480 frames / 10.00 ms
Min: 480 frames / 10.00 ms
Max: 480 frames / 10.00 ms
Initialize: S_OK
Current: 480 frames / 10.00 ms
```

JSON output should be stable enough for users to attach to issues.

## Phase 10: README Updates

Update README after the probe/keepalive implementation lands.

Explain:

- REAL does not patch or replace audio drivers.
- REAL asks Windows for the smallest shared-mode period the current driver path
  advertises.
- Different drivers, endpoints, roles, categories, raw mode, and APO paths can
  expose different periods.
- Generic HDAudio may work better because it exposes smaller constraints.
- Newer Realtek/OEM drivers may be genuinely hard-limited to 10 ms in shared
  mode.
- Raw mode can reduce latency but may bypass OEM tuning.
- Exclusive WASAPI, ASIO, and Kernel Streaming are alternatives for apps that
  support them, not global shared-mode fixes.

## Phase 11: Test Matrix

Test on real Windows hardware.

Minimum hardware/driver cases:

- Microsoft generic "High Definition Audio Device".
- Older Realtek HDA driver where REAL is known to work.
- Newer Realtek/OEM laptop where generic HDA cannot be force-installed.
- USB audio device.
- Bluetooth endpoint, mainly to verify graceful reporting.

For each system:

1. Run `REAL.exe --probe`.
2. Run `REAL.exe --probe --json`.
3. Run with default keepalive behavior.
4. Verify normal audio still plays.
5. Verify no crash on unsupported raw mode.
6. Verify no misleading success message if every mode is 10 ms.
7. Compare `GetCurrentSharedModeEnginePeriod` to the selected requested period.
8. Listen for glitches/crackles under light and moderate CPU load.

## Creative Workarounds To Treat As Research

These may be useful later, but should not be the first implementation.

### Disable Or Remove OEM APO Packages

Try disabling or uninstalling OEM enhancement stacks:

- Nahimic
- Sonic Studio
- DTS
- Dolby
- Waves
- MaxxAudio
- Realtek console effects

Then rerun the probe. This can change selected signal processing modes or make
raw/default paths simpler. It is not guaranteed to change driver packet
constraints.

### Extension INF Or Driver Package Tweaks

Microsoft extension INFs can override or add device configuration after the base
driver package. This is theoretically interesting if the base Realtek/OEM driver
stores low-latency constraints or APO bindings as configurable registry/INF
state.

Practical limits:

- If 10 ms is hardcoded in the miniport, SST, ACP, DSP, or firmware path, an INF
  cannot fix it.
- Production driver packages need signing.
- Extension INF ordering and targeting can be complex.
- This path is likely specific to one OEM driver package at a time.

### Kernel Filter Or Replacement Driver

A kernel-mode filter or replacement miniport could theoretically expose different
constraints, but that is a driver-signing, stability, and hardware-validation
project. It is not a normal app feature.

### ASIO Or Kernel Streaming

ASIO4ALL and Kernel Streaming may help apps that support ASIO/KS because they can
bypass parts of the shared-mode path. They do not automatically lower latency for
normal shared-mode apps such as browsers, games, or chat clients unless those
apps use the alternate API or are routed through a proxy that may add latency.

## Recommended First Milestone

Implement `REAL 0.3` as a diagnostic-first tool:

1. HRESULT-aware errors.
2. Endpoint and role enumeration.
3. Category/raw probing through `SetClientProperties`.
4. Text and JSON probe reports.
5. Best-stream selection.
6. Event-driven silent render keepalive.
7. Accurate success/failure messaging.

This gives hard evidence on each affected Realtek/OEM system. After that, decide
whether the next useful step is multi-mode keepalive, APO-disabling guidance,
extension-INF research, or declaring a given driver stack hard-limited from user
mode.
