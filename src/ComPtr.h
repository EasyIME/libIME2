//
//    Copyright (C) 2013 - 2020 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
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

#ifndef IME_COM_PTR_H
#define IME_COM_PTR_H

#include <utility>

// ATL-indepdent smart pointers for COM objects
// very similar to the ones provided by ATL (CComPtr & CComQIPtr).

namespace Ime {

// a smart pointer for COM interface/object
// automatic AddRef() on copy and Release() on destruction.
template <class T>
class ComPtr {
public:
    ComPtr(void): p_(nullptr) {
    }

    ComPtr(T* p): p_(p) {
        if (p_) {
            p_->AddRef();
        }
    }

    ComPtr(ComPtr&& other) noexcept : p_(other.p_) {
        other.p_ = nullptr;
    }

    ComPtr(const ComPtr& other): p_(other.p_) {
        if (p_) {
            p_->AddRef();
        }
    }

    ~ComPtr(void) {
        if(p_) {
            p_->Release();
        }
    }

    // Similar to std::make_unique()
    template <typename... Args>
    static ComPtr make(Args&&... args) {
        return ComPtr::takeover(new T(std::forward<Args>(args)...));
    }

    static ComPtr takeover(T*&& rawPtr) {
        ComPtr ptr;
        ptr.p_ = rawPtr;
        rawPtr = nullptr;
        return ptr;
    }

    // QueryInterface
    template <typename U>
    ComPtr<U> query() const {
        return ComPtr<U>::queryFrom(p_);
    }

    // QueryInterface
    static ComPtr<T> queryFrom(IUnknown* p) {
        ComPtr<T> result;
        if(p != nullptr) {
            p->QueryInterface(__uuidof(T), (void**)&result);
        }
        return result;
    }

    T& operator * () const {
        return *p_;
    }

    T** operator & () {
        return &p_;
    }

    T* operator-> () const {
        return p_;
    }

    operator T* () const {
        return p_;
    }

    bool operator !() const {
        return !p_;
    }

    bool operator == (T* p) const {
        return p == p_;
    }

    bool operator != (T* p) const {
        return p != p_;
    }

    bool operator < (T* p) const {
        return p_ < p;
    }

    ComPtr& operator = (ComPtr&& other) noexcept {
        p_ = other.p_;
        other.p_ = nullptr;
        return *this;
    }

    ComPtr& operator = (const ComPtr& other) {
        return operator = (other.p_);
    }

    ComPtr& operator = (T* p) {
        T* old = p_;
        p_ = p;
        if (p_) {
            p_->AddRef();
        }
        if (old) {
            old->Release();
        }
        return *this;
    }

protected:
    T* p_;
};

}

#endif
