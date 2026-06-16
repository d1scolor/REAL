#pragma once

#include "../ExpectedError.h"

namespace miniant::Windows {

class WindowsError : public ExpectedError {
public:
    WindowsError();
    static WindowsError FromHRESULT(long hr, const std::string& context);
    static WindowsError FromLastError(const std::string& context);

    WindowsError(std::string message) noexcept:
        ExpectedError(std::move(message)) {}

    WindowsError(const char* message):
        ExpectedError(message) {}
};

}
