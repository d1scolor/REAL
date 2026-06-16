#include "AutoUpdater.h"
#include "OStreamSink.h"

#include "Windows/AudioProbe.h"
#include "Windows/Console.h"
#include "Windows/LowLatencyRenderStream.h"
#include "Windows/MessagingWindow.h"
#include "Windows/TrayIcon.h"

#include "../res/resource.h"

#include <spdlog/spdlog.h>

#include <conio.h>
#include <Shellapi.h>

#include <algorithm>
#include <locale>
#include <iostream>
#include <sstream>

using namespace miniant::AutoUpdater;
using namespace miniant::Spdlog;
using namespace miniant::Windows;
using namespace miniant::Windows::WasapiLatency;

constexpr Version APP_VERSION(0, 3, 0);

struct CommandLineOptions {
    bool tray = false;
    bool probeOnly = false;
    bool json = false;
    bool allEndpoints = false;
    bool verboseFailures = true;
    bool waitAfterProbe = true;
    int keepBest = 1;
    double thresholdMs = 10.0;
    RawMode rawMode = RawMode::Auto;
    std::vector<EndpointRole> roles = {
        EndpointRole::Console,
        EndpointRole::Multimedia,
        EndpointRole::Communications
    };
    std::vector<StreamCategory> categories = {
        StreamCategory::Media,
        StreamCategory::GameMedia,
        StreamCategory::GameEffects,
        StreamCategory::Communications,
        StreamCategory::Other
    };
};

void WaitForAnyKey(const std::string& message) {
    while (_kbhit()) {
        _getch();
    }

    spdlog::get("app_out")->info(message);
    _getch();
}

void DisplayExitMessage(bool success) {
    if (success) {
        WaitForAnyKey("\nPress any key to disable and exit . . .");
    } else {
        WaitForAnyKey("\nPress any key to exit . . .");
    }
}

std::string ToLower(const std::string& string) {
    std::string result;
    for (const auto& c : string) {
        result.append(1, std::tolower(c, std::locale()));
    }

    return result;
}

std::string ToUtf8(const std::wstring& value) {
    return WStringToUtf8(value);
}

std::vector<std::wstring> GetArguments() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::vector<std::wstring> arguments;
    if (argv == nullptr) {
        return arguments;
    }

    for (int i = 1; i < argc; ++i) {
        arguments.emplace_back(argv[i]);
    }

    ::LocalFree(argv);
    return arguments;
}

std::vector<EndpointRole> ParseRoles(const std::wstring& value) {
    if (value == L"console") {
        return { EndpointRole::Console };
    }
    if (value == L"multimedia") {
        return { EndpointRole::Multimedia };
    }
    if (value == L"communications") {
        return { EndpointRole::Communications };
    }

    return { EndpointRole::Console, EndpointRole::Multimedia, EndpointRole::Communications };
}

std::vector<StreamCategory> ParseCategories(const std::wstring& value) {
    if (value == L"media") {
        return { StreamCategory::Media };
    }
    if (value == L"game") {
        return { StreamCategory::GameMedia, StreamCategory::GameEffects };
    }
    if (value == L"communications") {
        return { StreamCategory::Communications };
    }
    if (value == L"other") {
        return { StreamCategory::Other };
    }

    return {
        StreamCategory::Media,
        StreamCategory::GameMedia,
        StreamCategory::GameEffects,
        StreamCategory::Communications,
        StreamCategory::Other,
        StreamCategory::Movie,
        StreamCategory::SoundEffects
    };
}

CommandLineOptions ParseCommandLine() {
    CommandLineOptions options;
    auto arguments = GetArguments();

    for (size_t i = 0; i < arguments.size(); ++i) {
        const auto& argument = arguments[i];
        if (argument == L"--tray") {
            options.tray = true;
        } else if (argument == L"--probe") {
            options.probeOnly = true;
        } else if (argument == L"--json") {
            options.json = true;
            options.probeOnly = true;
            options.waitAfterProbe = false;
        } else if (argument == L"--all-endpoints") {
            options.allEndpoints = true;
        } else if (argument == L"--no-wait") {
            options.waitAfterProbe = false;
        } else if (argument == L"--roles" && i + 1 < arguments.size()) {
            options.roles = ParseRoles(arguments[++i]);
        } else if (argument == L"--categories" && i + 1 < arguments.size()) {
            options.categories = ParseCategories(arguments[++i]);
        } else if (argument == L"--raw" && i + 1 < arguments.size()) {
            const auto mode = arguments[++i];
            if (mode == L"on") {
                options.rawMode = RawMode::On;
            } else if (mode == L"off") {
                options.rawMode = RawMode::Off;
            } else {
                options.rawMode = RawMode::Auto;
            }
        } else if (argument == L"--keep-best" && i + 1 < arguments.size()) {
            options.keepBest = std::max(1, std::stoi(arguments[++i]));
        } else if (argument == L"--threshold-ms" && i + 1 < arguments.size()) {
            options.thresholdMs = std::stod(arguments[++i]);
        }
    }

    return options;
}

IMMDevice* FindDeviceForProbe(const std::vector<EndpointSelection>& endpoints, const ProbeResult& probe) {
    auto it = std::find_if(endpoints.begin(), endpoints.end(), [&](const EndpointSelection& endpoint) {
        return endpoint.info.id == probe.endpoint.id;
        });
    if (it == endpoints.end()) {
        return nullptr;
    }

    return it->device.Get();
}

void PrintSelectedProbe(const ProbeResult& probe, bool keepaliveActive) {
    auto app_out = spdlog::get("app_out");
    app_out->info("Selected endpoint: {}", ToUtf8(probe.endpoint.friendlyName));
    app_out->info("Role: {}", ToString(probe.options.role));
    app_out->info("Category: {}", ToString(probe.options.category));
    app_out->info("Raw mode: {}", probe.options.raw ? "enabled" : "disabled");
    app_out->info(
        "Driver minimum: {} frames / {:.2f} ms",
        probe.periods.minFrames,
        probe.periods.minMs);
    app_out->info(
        "Default period: {} frames / {:.2f} ms",
        probe.periods.defaultFrames,
        probe.periods.defaultMs);
    if (probe.periods.currentFrames != 0) {
        app_out->info(
            "Current engine period: {} frames / {:.2f} ms",
            probe.periods.currentFrames,
            probe.periods.currentMs);
    }
    app_out->info("Keepalive: {}\n", keepaliveActive ? "active" : "inactive");
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    auto oss = std::make_shared<std::ostringstream>();
    auto sink = std::make_shared<OStreamSink>(oss, true);
    auto app_out = std::make_shared<spdlog::logger>("app_out", sink);
    app_out->set_pattern("%v");
    spdlog::register_logger(app_out);

    auto console = std::make_shared<Console>([=] {
        std::lock_guard<std::mutex> lock(sink->GetMutex());
        std::cout << oss->str();
        std::cout.flush();
        oss->set_rdbuf(std::cout.rdbuf());
        });

    std::unique_ptr<MessagingWindow> window;
    std::unique_ptr<TrayIcon> trayIcon;

    CommandLineOptions options = ParseCommandLine();
    bool success = true;

    if (options.tray && !options.probeOnly) {
        tl::expected windowPtrResult = MessagingWindow::CreatePtr();
        if (!windowPtrResult) {
#pragma push_macro("GetMessage")
#undef GetMessage
            app_out->error("Error: {}", windowPtrResult.error().GetMessage());
            return 1;
        }

        window = std::move(*windowPtrResult);
        HICON hIcon = ::LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
        trayIcon = std::make_unique<TrayIcon>(*window, hIcon);
        trayIcon->SetLButtonUpHandler([=, &success](TrayIcon& trayIcon) {
            trayIcon.Hide();
            console->Open();

            DisplayExitMessage(success);

            console->Close();
            return std::optional<LRESULT>();
            });
        trayIcon->Show();
    } else {
        console->Open();
    }

    app_out->info("REAL - REduce Audio Latency {}, mini)(ant, 2018-2019", APP_VERSION.ToString());
    app_out->info("Project: https://github.com/miniant-git/REAL\n");

    ComApartment apartment;
    std::vector<std::unique_ptr<LowLatencyRenderStream>> streams;
    if (!apartment.IsUsable()) {
        success = false;
        app_out->info("ERROR: {}\n", WindowsError::FromHRESULT(apartment.Result(), "CoInitializeEx failed").GetMessage());
    } else {
        auto endpoints = EnumerateRenderEndpoints(options.roles, options.allEndpoints);
        if (!endpoints) {
            success = false;
            app_out->info("ERROR: {}\n", endpoints.error().GetMessage());
        } else {
            auto results = ProbeEndpoints(*endpoints, options.categories, options.rawMode);
            if (options.json) {
                app_out->info(FormatProbeJson(results));
                return 0;
            }
            if (options.probeOnly) {
                app_out->info(FormatProbeTable(results, options.verboseFailures));
                if (options.waitAfterProbe) {
                    WaitForAnyKey("\nPress any key to exit . . .");
                }
                return 0;
            }

            auto selected = SelectBestProbes(results, static_cast<size_t>(options.keepBest));
            if (selected.empty()) {
                success = false;
                app_out->info("ERROR: No tested shared-mode stream could be initialized.\n");
                app_out->info(FormatProbeTable(results, true));
            } else {
                const bool improved = selected.front()->periods.minMs < options.thresholdMs;
                if (!improved) {
                    app_out->info("No shared-mode path below {:.2f} ms was found.", options.thresholdMs);
                    app_out->info("The installed driver reports {:.2f} ms as its best minimum for the tested categories, roles, and raw/non-raw modes.", selected.front()->periods.minMs);
                    app_out->info("REAL cannot force a lower shared-mode period from user mode.\n");
                }

                for (const auto* probe : selected) {
                    IMMDevice* device = FindDeviceForProbe(*endpoints, *probe);
                    if (device == nullptr) {
                        success = false;
                        app_out->info("ERROR: Could not find selected endpoint for keepalive startup.\n");
                        continue;
                    }

                    auto stream = LowLatencyRenderStream::Start(device, *probe);
                    if (!stream) {
                        success = false;
                        app_out->info("ERROR: Could not start keepalive stream: {}\n", stream.error().GetMessage());
                        continue;
                    }

                    PrintSelectedProbe(*probe, true);
                    streams.push_back(std::move(*stream));
                }
            }
        }
    }

#if REAL_ENABLE_UPDATER
    AutoUpdater updater;
    tl::expected cleanupResult = updater.CleanupPreviousSetup();
    if (!cleanupResult) {
        app_out->info("Error: {}", cleanupResult.error().GetMessage());
    }

    if (auto notes = updater.IsAppSuperseded(); notes) {
        if (trayIcon != nullptr) {
            trayIcon->Hide();
        }

        console->Open();

        app_out->info(*notes);

        DisplayExitMessage(success);
        console->Close();
        return 0;
    }

    app_out->info("Checking for updates...");
    tl::expected info = updater.GetUpdateInfo();
    if (!info) {
        app_out->info("Error: {}", info.error().GetMessage());
        app_out->info("Update failed!");
    } else if (info->version > APP_VERSION) {
        if (trayIcon != nullptr) {
            trayIcon->Hide();
        }

        console->Open();

        app_out->info("A new update is available!");
        if (info->releaseNotes) {
            app_out->info(*info->releaseNotes);
        }

        std::cout << "Do you want to update to " << info->version.ToString() << "? [y/N] : ";
        char line[5];
        std::cin.getline(line, 5);
        std::string prompt(ToLower(line));
        if (prompt == "y" || prompt == "yes") {
            auto status = updater.ApplyUpdate(*info);
            if (status) {
                app_out->info("Updated successfully! Restart the application to apply changes.");
            } else {
                app_out->info("Update failed.");
                app_out->info("Error: {}", status.error().GetMessage());
            }

        } else {
            app_out->info("No: Keeping the current version.");
        }

        DisplayExitMessage(success);
        return 2;
    } else {
        app_out->info("The application is up-to-date.");
    }
#else
    app_out->info("Automatic updates are disabled in this build.");
#endif

#pragma pop_macro("GetMessage")
    if (options.tray && !options.probeOnly) {
        MSG msg;
        while (::GetMessage(&msg, NULL, 0, 0) > 0) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
    } else {
        DisplayExitMessage(success);
    }

    return 0;
}
