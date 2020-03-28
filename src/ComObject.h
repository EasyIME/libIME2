//
//    Copyright (C) 2020 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
//
//    This library is free software; you can redistribute it and/or
//    modify it under the terms of the GNU Library General Public
//    License as published by the Free Software Foundation; either
//    version 2 of the License, or (at your option) any later version.
//
//    This library is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//    Library General Public License for more details.
//
//    You should have received a copy of the GNU Library General Public
//    License along with this library; if not, write to the
//    Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
//    Boston, MA  02110-1301, USA.
//

#pragma once

#include <Unknwn.h>
#include <cassert>

namespace Ime {

template <typename Base, typename... Interfaces>
class ComInterface: public Base {
protected:
    void* queryInterface(REFIID riid) {
        return queryInterfaceHelper<Base, Interfaces...>(riid);
    }

private:
    template <typename T>
    void* queryInterfaceHelper(REFIID riid) {
        return riid == __uuidof(T) ? static_cast<T*>(this) : nullptr;
    }

    template <typename T, typename U, typename... Args>
    void* queryInterfaceHelper(REFIID riid) {
        return riid == __uuidof(T) ? static_cast<T*>(this) : queryInterfaceHelper<U, Args...>(riid);
    }
};


template <typename FirstInterface, typename... ComInterfaces>
class ComObject : public FirstInterface, public ComInterfaces... {
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
            // An explicit type casting here is required to ensure querying IUnknown
            // always returns the same pointer. (This is required by COM).
            *ppvObj = static_cast<FirstInterface*>(this);
        }
        else {
            *ppvObj = queryInterfaceHelper<FirstInterface, ComInterfaces...>(riid);
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
    void* queryInterfaceHelper(REFIID riid) {
        return T::queryInterface(riid);
    }

    template <typename T, typename U, typename... Args>
    void* queryInterfaceHelper(REFIID riid) {
        auto result = T::queryInterface(riid);
        return result ? result : queryInterfaceHelper<U, Args...>(riid);
    }

private:
    int refCount_;
};

} // namespace Ime
