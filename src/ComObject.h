#pragma once

#include <Unknwn.h>
#include <cassert>

namespace Ime {

// When the Interfaces list contains some COM interfaces which are the bases of the others,
// this can trigger warning C4584: base-class 'IXXX' is already a base-class.
// In COM, all these interface classes have no implementations and only provide a vtable.
// So having duplicated instances of base classes is quite cheap. We only wasted space of
// some duplicated function pointers in the vtables. So we ignore this warning.
// This is a tradeoff since if you don't list the base classes, QueryInterface for them
// based on variadic template won't work.
#pragma warning(disable:4584)

// Common base class to implement COM objects.
template <typename... Interfaces>
class ComObject : public Interfaces... {
public:
    ComObject() : refCount_{ 1 } {}

    virtual ~ComObject() {}

    int refCount() const {
        return refCount_;
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) {
        if (ppvObj == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown) {
            *ppvObj = this;
        }
        else {
            *ppvObj = queryInterface<Interfaces...>(riid);
        }
        if (*ppvObj) {
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() {
        return ++refCount_;
    }

    STDMETHODIMP_(ULONG) Release() {
        assert(refCount_ > 0);
        const ULONG newCount = --refCount_;
        if (0 == refCount_) {
            delete this;
        }
        return newCount;
    }

private:

    template <typename T>
    void* queryInterface(REFIID riid) {
        return riid == __uuidof(T) ? static_cast<T*>(this) : nullptr;
    }

    template <typename T, typename U, typename... Args>
    void* queryInterface(REFIID riid) {
        return riid == __uuidof(T) ? static_cast<T*>(this) : queryInterface<U, Args...>(riid);
    }

private:
    int refCount_;
};

#pragma warning(default:4584)

} // namespace Ime
