#pragma once

#include <msctf.h>
#include "ComPtr.h"

namespace Ime {

class SinkAdvice {
public:
    SinkAdvice() : cookie_{ TF_INVALID_COOKIE } {}

    SinkAdvice(ITfSource* source, REFIID riid, IUnknown* sink) : SinkAdvice() {
        DWORD cookie;
        if (source->AdviseSink(riid, sink, &cookie) == S_OK) {
            source_ = source;
            cookie_ = cookie;
        }
    }

    SinkAdvice(const SinkAdvice&) = delete;

    SinkAdvice(SinkAdvice&& other) noexcept : source_{ other.source_ }, cookie_{ other.cookie_ } {
        other.source_ = nullptr;
        other.cookie_ = TF_INVALID_COOKIE;
    }

    ~SinkAdvice() {
        unadvise();
    }

    SinkAdvice& operator = (SinkAdvice&& other) = default;

    void unadvise() {
        if (source_ && cookie_ != TF_INVALID_COOKIE) {
            (source_->UnadviseSink)(cookie_);
            source_ = nullptr;
            cookie_ = TF_INVALID_COOKIE;
        }
    }

private:
    ComPtr<ITfSource> source_;
    DWORD cookie_;
};

} // namespace Ime
