#pragma once

#include <utility>

namespace miniant::Windows {

template<typename T>
class ComPtr {
public:
    ComPtr() = default;

    explicit ComPtr(T* ptr) noexcept:
        m_ptr(ptr) {}

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept:
        m_ptr(other.m_ptr) {
        other.m_ptr = nullptr;
    }

    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
        }

        return *this;
    }

    ~ComPtr() {
        Reset();
    }

    T* Get() const noexcept {
        return m_ptr;
    }

    T** Put() noexcept {
        Reset();
        return &m_ptr;
    }

    T* Detach() noexcept {
        T* ptr = m_ptr;
        m_ptr = nullptr;
        return ptr;
    }

    void Reset(T* ptr = nullptr) noexcept {
        if (m_ptr != nullptr) {
            m_ptr->Release();
        }

        m_ptr = ptr;
    }

    T* operator->() const noexcept {
        return m_ptr;
    }

    explicit operator bool() const noexcept {
        return m_ptr != nullptr;
    }

private:
    T* m_ptr = nullptr;
};

}
