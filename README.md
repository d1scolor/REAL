![REAL](img/logo.png)

---

## Features

* Audio latency probing across default playback roles and active render endpoints
* Category and raw-mode WASAPI diagnostics
* Event-driven silent keepalive stream for the best supported shared-mode period
* Text and JSON probe reports
* Automatic updates
* Starting minimised

## Requirements

* Windows 10 64-bit
* [Microsoft Visual C++ 2017 Redistributable (x64)](https://aka.ms/vs/15/release/VC_redist.x64.exe) 

## Setup

1. Install Windows' in-box HDAudio driver (optional, might improve latency):
    1. Start **Device Manager**.
    2. Under **Sound, video and game controllers**, double click on the device that corresponds to your speakers.
    3. In the next window, go to the **Driver** tab.
    4. Select **Update driver** -> **Browse my computer for driver software** -> **Let me pick from a list of available drivers on my computer**.
    5. Select **High Definition Audio Device** and click **Next**.
    6. If a window titled "Update Driver warning" appears, click **Yes**.
    7. Select **Close**.
    8. If asked to reboot the system, select **Yes** to reboot.
    > **Be careful**: the new driver might reset your volume to uncomfortably high levels. 
2. Download the [latest version](https://github.com/miniant-git/REAL/releases/latest) of **REAL**.
3. Launch `REAL.exe`. REAL probes the current Windows audio stack, starts the best valid shared-mode keepalive stream, and keeps it active as long as the application is running.

## Command-Line Options
* `--tray` Launches the application minimised to the system tray
* `--probe` Prints a diagnostic probe report and waits for a key before exiting
* `--json` Prints the probe report as JSON and exits immediately
* `--all-endpoints` Probes all active render endpoints in addition to default role endpoints
* `--no-wait` Exits immediately after text probe output
* `--roles all|console|multimedia|communications` Selects default render roles to probe
* `--categories all|media|game|communications|other` Selects WASAPI stream categories to probe
* `--raw auto|on|off` Selects raw processing mode probing
* `--keep-best N` Keeps the best `N` successfully initialized streams alive
* `--threshold-ms 10` Sets the latency threshold used for success/failure messaging

## Building

1. Make sure **Visual Studio 2022** or **Visual Studio Build Tools** is installed with a Windows 10/11 SDK.
2. Install [CMake 3.12](https://cmake.org/download/) or later and configure your `PATH` environment variable to find `cmake` if necessary.
3. Clone the repository:
    ```bat
    git clone https://github.com/miniant-git/REAL.git miniant-real
    cd miniant-real
    ```
4. Configure Visual Studio project with CMake:
   ```bat
   cd real-app
   cmake -S . -B build -DREAL_ENABLE_UPDATER=OFF
   ```
   `REAL_ENABLE_UPDATER=OFF` is the default local build mode. It avoids the legacy curl bootstrap and disables automatic update checks.
5. Open the generated solution:
   ```bat
   start build/miniant-real.sln
   ```
6. Right-click on the `real-app` project in the **Solution Explorer** and select **Build**.
   * The resulting executable will be placed inside `real-app/build/Debug/` folder.

To build the legacy curl-based updater path, configure with:

```bat
cmake -S . -B build -DREAL_ENABLE_UPDATER=ON
```

That mode requires the old curl external-project bootstrap to work in the local Git/Visual Studio environment.

## FAQ

### How does this work?

REAL does not patch, replace, or override audio drivers. It asks Windows for the smallest shared-mode engine period that the current endpoint and driver path advertises, then keeps a silent render stream active at the best supported period.

Different drivers, endpoints, default roles, stream categories, raw processing mode, and OEM APO paths can expose different period constraints. REAL probes those combinations first so the result is visible instead of assuming the default playback path can go below 10 ms.

Microsoft's generic **High Definition Audio Device** driver may work better on some older HDAudio systems because it often exposes smaller shared-mode constraints than OEM Realtek packages. Newer Realtek/OEM laptop paths involving Intel SST, AMD ACP, SoundWire, DSP firmware, speaker protection, Dolby/DTS/Waves/Nahimic-style APOs, or similar components may genuinely report 10 ms as their minimum shared-mode period.

If every tested path reports 10 ms, REAL cannot force a lower shared-mode period from user mode. Apps that support WASAPI exclusive mode, ASIO, or Kernel Streaming may still be able to use lower-latency paths for their own audio, but those APIs do not globally lower shared-mode latency for browsers, games, chat apps, or other normal shared-mode clients.

Raw mode can reduce latency on some systems by bypassing selected signal processing, but it may also bypass OEM speaker tuning or effects. REAL reports raw-mode failures as unsupported diagnostics instead of treating them as application failures.

### What are the downsides?

Since the application reduces audio sample buffer size, the buffer runs out faster and needs to be refilled more frequently. This increases the odds of audible audio cracks appearing when the CPU is busy and unable to keep up. 
