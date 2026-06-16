#include "WindowsError.h"

#include <Windows.h>

#include <iomanip>
#include <sstream>
#include <unordered_map>

#ifdef GetMessage
#undef GetMessage
#endif

using namespace miniant::Windows;

namespace {

std::string FormatSystemMessage(DWORD error) {
    LPSTR buffer = nullptr;
    DWORD size = ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);

    if (size == 0 || buffer == nullptr) {
        return {};
    }

    std::string message(buffer, size);
    ::LocalFree(buffer);

    while (!message.empty() && (message.back() == '\r' || message.back() == '\n' || message.back() == ' ')) {
        message.pop_back();
    }

    return message;
}

std::string HRESULTName(HRESULT hr) {
    static const std::unordered_map<HRESULT, const char*> names = {
        { S_OK, "S_OK" },
        { S_FALSE, "S_FALSE" },
        { E_POINTER, "E_POINTER" },
        { E_INVALIDARG, "E_INVALIDARG" },
        { E_OUTOFMEMORY, "E_OUTOFMEMORY" },
        { E_NOTIMPL, "E_NOTIMPL" },
        { E_NOINTERFACE, "E_NOINTERFACE" },
        { E_ACCESSDENIED, "E_ACCESSDENIED" },
        { HRESULT_FROM_WIN32(ERROR_NOT_FOUND), "HRESULT_FROM_WIN32(ERROR_NOT_FOUND)" },
        { 0x88890001, "AUDCLNT_E_NOT_INITIALIZED" },
        { 0x88890002, "AUDCLNT_E_ALREADY_INITIALIZED" },
        { 0x88890003, "AUDCLNT_E_WRONG_ENDPOINT_TYPE" },
        { 0x88890004, "AUDCLNT_E_DEVICE_INVALIDATED" },
        { 0x88890005, "AUDCLNT_E_NOT_STOPPED" },
        { 0x88890006, "AUDCLNT_E_BUFFER_TOO_LARGE" },
        { 0x88890007, "AUDCLNT_E_OUT_OF_ORDER" },
        { 0x88890008, "AUDCLNT_E_UNSUPPORTED_FORMAT" },
        { 0x88890009, "AUDCLNT_E_INVALID_SIZE" },
        { 0x8889000A, "AUDCLNT_E_DEVICE_IN_USE" },
        { 0x8889000B, "AUDCLNT_E_BUFFER_OPERATION_PENDING" },
        { 0x8889000C, "AUDCLNT_E_THREAD_NOT_REGISTERED" },
        { 0x8889000D, "AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED" },
        { 0x8889000E, "AUDCLNT_E_ENDPOINT_CREATE_FAILED" },
        { 0x8889000F, "AUDCLNT_E_SERVICE_NOT_RUNNING" },
        { 0x88890010, "AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED" },
        { 0x88890011, "AUDCLNT_E_EXCLUSIVE_MODE_ONLY" },
        { 0x88890012, "AUDCLNT_E_BUFDURATION_PERIOD_NOT_EQUAL" },
        { 0x88890013, "AUDCLNT_E_EVENTHANDLE_NOT_SET" },
        { 0x88890014, "AUDCLNT_E_INCORRECT_BUFFER_SIZE" },
        { 0x88890015, "AUDCLNT_E_BUFFER_SIZE_ERROR" },
        { 0x88890016, "AUDCLNT_E_CPUUSAGE_EXCEEDED" },
        { 0x88890017, "AUDCLNT_E_BUFFER_ERROR" },
        { 0x88890018, "AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED" },
        { 0x88890019, "AUDCLNT_E_INVALID_DEVICE_PERIOD" },
        { 0x88890020, "AUDCLNT_E_ENGINE_PERIODICITY_LOCKED" },
        { 0x88890021, "AUDCLNT_E_ENGINE_FORMAT_LOCKED" },
        { 0x88890022, "AUDCLNT_E_HEADTRACKING_ENABLED" },
    };

    auto it = names.find(hr);
    if (it == names.end()) {
        return {};
    }

    return it->second;
}

std::string FormatHRESULT(HRESULT hr, const std::string& context) {
    std::ostringstream oss;
    oss << context << ": 0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
        << static_cast<unsigned long>(hr);

    const auto name = HRESULTName(hr);
    if (!name.empty()) {
        oss << " " << name;
    }

    const auto systemMessage = FormatSystemMessage(static_cast<DWORD>(hr));
    if (!systemMessage.empty()) {
        oss << " - " << systemMessage;
    }

    return oss.str();
}

std::string GetErrorMessage() {
    return WindowsError::FromLastError("Windows API call failed").GetMessage();
}

}

WindowsError::WindowsError():
    ExpectedError(GetErrorMessage()) {}

WindowsError WindowsError::FromHRESULT(long hr, const std::string& context) {
    return WindowsError(FormatHRESULT(static_cast<HRESULT>(hr), context));
}

WindowsError WindowsError::FromLastError(const std::string& context) {
    const DWORD lastError = ::GetLastError();
    std::ostringstream oss;
    oss << context << ": " << lastError;

    const auto systemMessage = FormatSystemMessage(lastError);
    if (!systemMessage.empty()) {
        oss << " - " << systemMessage;
    }

    return WindowsError(oss.str());
}
