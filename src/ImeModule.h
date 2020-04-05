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

#ifndef IME_IME_MODULE_H
#define IME_IME_MODULE_H

#include <Unknwn.h>
#include <Windows.h>

#include <Ctffunc.h>
#include <string>
#include <list>
#include "ComPtr.h"
#include "ComObject.h"
#include <mutex>

namespace Ime {

class TextService;
class DisplayAttributeInfo;

// language profile info, used to register new language profiles
struct LangProfileInfo {
    std::wstring name; // should not exceed 32 chars
    GUID profileGuid;
    std::wstring locale; // RFC 4646 locale name
    std::wstring fallbackLocale; // RFC 1766 locale name or other alternatives if locale does not work
    std::wstring iconFile;
    int iconIndex;
};


class ImeModule: public ComObject<
    ComInterface<IClassFactory>,
    ComInterface<ITfFnConfigure>
> {
public:
    ImeModule(HMODULE module, const CLSID& textServiceClsid);

    // public methods
    HINSTANCE hInstance() const {
        return hInstance_;
    }

    const CLSID& textServiceClsid() const {
        return textServiceClsid_;
    }

    // Dll entry points implementations
    HRESULT canUnloadNow();
    HRESULT getClassObject(REFCLSID rclsid, REFIID riid, void **ppvObj);

    HRESULT registerServer(wchar_t* imeName, LangProfileInfo* langs, int count);
    HRESULT registerLangProfiles(LangProfileInfo* langs, int count);
    HRESULT unregisterServer();

    // should be override by IME implementors
    virtual TextService* createTextService() = 0;

    // called when config dialog needs to be launched
    virtual bool onConfigure(HWND hwndParent, LANGID langid, REFGUID rguidProfile);

    // display attributes for composition string
    std::list<ComPtr<DisplayAttributeInfo>>& displayAttrInfos() {
        return displayAttrInfos_;
    }

    bool registerDisplayAttributeInfos();

    DisplayAttributeInfo* inputAttrib() {
        return inputAttrib_;
    }

    /*
    DisplayAttributeInfo* convertedAttrib() {
        return convertedAttrib_;
    }
    */

    // COM-related stuff

    // IUnknown
    STDMETHODIMP_(ULONG) AddRef(void) override;
    STDMETHODIMP_(ULONG) Release(void) override;

protected:
    // IClassFactory
    STDMETHODIMP CreateInstance(IUnknown *pUnkOuter, REFIID riid, void **ppvObj);
    STDMETHODIMP LockServer(BOOL fLock);

    // ITfFunction
    STDMETHODIMP GetDisplayName(BSTR *pbstrName);

    // ITfFnConfigure
    STDMETHODIMP Show(HWND hwndParent, LANGID langid, REFGUID rguidProfile);

protected: // COM object should not be deleted directly. calling Release() instead.
    virtual ~ImeModule(void);

private:
    // refCountMutex needs to be static because it may be accessed after Release() calls the destructor.
    static std::mutex refCountMutex_;
    HINSTANCE hInstance_;
    CLSID textServiceClsid_;

    // display attributes
    std::list< ComPtr<DisplayAttributeInfo>> displayAttrInfos_;
    ComPtr<DisplayAttributeInfo> inputAttrib_;
    // DisplayAttributeInfo* convertedAttrib_;
};

}

#endif
