#pragma once

template <typename T, void (*Deleter)(T **)>
class FreePointer {
public:
    explicit FreePointer(T *ptr = nullptr) : ptr_(ptr) {}
    ~FreePointer() { reset(); }

    // Disable copy semantics
    FreePointer(const FreePointer &) = delete;
    FreePointer &operator=(const FreePointer &) = delete;

    // Enable move semantics
    FreePointer(FreePointer &&other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }
    FreePointer &operator=(FreePointer &&other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    T *get() const { return ptr_; }
    T *release() {
        T *temp = ptr_;
        ptr_ = nullptr;
        return temp;
    }
    void reset(T *ptr = nullptr) {
        Deleter(&ptr_);
        ptr_ = ptr;
    }

private:
    T *ptr_;
};
