#pragma once

#include <cstring>
#include <new>
#include <string_view>

class CString {
public:
    explicit CString(const std::string_view &sv) {
        const auto *str_data = sv.data();
        auto str_size = sv.size();
        if (str_data[str_size]) {
            is_allocated_ = true;
            auto *str = new char[str_size + 1];
            std::memcpy(str, str_data, str_size);
            str[str_size] = '\0';
            str_ = str;
        } else {
            str_ = str_data;
        }
    }

    operator const char *() const { return str_; }

    [[nodiscard]] auto get() const { return str_; }

    ~CString() {
        if (is_allocated_) delete[] str_;
    }

private:
    const char *str_{};
    bool is_allocated_{};
};
