# REAL v0.3.0-beta.1 - Diagnostic low-latency probing

REAL v0.3.0-beta.1 is a diagnostic-focused release for testing what low-latency shared-mode periods your Windows audio driver actually exposes.

This release is meaningfully different from v0.2.0: instead of only trying the default playback device and reporting success optimistically, REAL now probes endpoints, stream categories, and raw/non-raw modes before selecting a keepalive stream.

## What's New

- Added WASAPI shared-mode latency probing.
- Added support for probing all active render endpoints.
- Added probing across default roles, stream categories, and raw processing mode.
- Added text diagnostic reports.
- Added JSON diagnostic output for issue reports and hardware comparisons.
- Added best-stream selection based on successfully initialized low-latency streams.
- Added a proper event-driven silent render keepalive loop.
- Added clearer reporting when a driver is hard-limited to 10 ms from user mode.
- Added HRESULT-aware error output for WASAPI failures.

## Usage

Run a diagnostic report:

```powershell
REAL.exe --probe --all-endpoints
```

Generate JSON output:

```powershell
REAL.exe --probe --all-endpoints --json
```

Start REAL using the best detected endpoint/path:

```powershell
REAL.exe --all-endpoints
```

## Important Notes

REAL does not patch, replace, or override audio drivers. It asks Windows for the smallest shared-mode engine period that the current endpoint and driver path advertises.

If every tested mode reports:

```text
Min: 10.00 ms
```

then REAL cannot force that endpoint below 10 ms from a normal user-mode WASAPI shared-mode app.

Some external USB DACs or audio interfaces may expose lower periods, while many Realtek/OEM/laptop driver stacks still report 10 ms as their minimum.

## Known Limitations

- This is a beta diagnostic release.
- Hardware and driver coverage is still limited.
- Automatic updates are disabled in this build.
- Some endpoint names may depend on console/font/language handling.
- WASAPI exclusive mode, ASIO, and Kernel Streaming are not global shared-mode fixes and are not what REAL currently configures.

## Asset

For a basic GitHub release, build the Release configuration executable and upload it as:

```text
REAL-v0.3.0-beta.1.exe
```

Recommended build command:

```powershell
cmake --build real-app\build --config Release --target real-app
```

Then rename:

```text
real-app\build\Release\real-app.exe
```

to:

```text
REAL-v0.3.0-beta.1.exe
```

Do not upload `real-app\build\Debug\real-app.exe`. Debug builds depend on non-redistributable Visual Studio debug runtime DLLs such as `VCRUNTIME140D.dll`, `MSVCP140D.dll`, and `ucrtbased.dll`.

Release builds may still require the Microsoft Visual C++ 2015-2022 Redistributable (x64) on machines that do not already have the release runtime installed.
