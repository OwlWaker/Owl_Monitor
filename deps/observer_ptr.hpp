#pragma once

#include <compare>
#include <cstddef>


template<typename T>
class observer_ptr {
public:
    using element_type = T;

    constexpr observer_ptr() noexcept : ptr_(nullptr) {}
    constexpr observer_ptr(std::nullptr_t) noexcept : ptr_(nullptr) {}
    constexpr /* implicit */ observer_ptr(T* p) noexcept : ptr_(p) {}

    observer_ptr& operator=(T* p) noexcept { ptr_ = p; return *this; }
    observer_ptr& operator=(std::nullptr_t) noexcept { ptr_ = nullptr; return *this; }

    constexpr T* get() const noexcept { return ptr_; }

    constexpr T& operator*() const noexcept { return *ptr_; }
    constexpr T* operator->() const noexcept { return ptr_; }

    constexpr explicit operator bool() const noexcept { return ptr_ != nullptr; }

    constexpr T* release() noexcept { T* p = ptr_; ptr_ = nullptr; return p; }
    constexpr void reset(T* p = nullptr) noexcept { ptr_ = p; }

    friend constexpr auto operator<=>(observer_ptr, observer_ptr) noexcept = default;

private:
    T* ptr_;
};

template<typename T>
constexpr observer_ptr<T> make_observer(T* p) noexcept { return observer_ptr<T>(p); }

