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

#include "LangBarButton.h"
#include "TextService.h"
#include "ImeModule.h"
#include <OleCtl.h>
#include <assert.h>
#include <stdlib.h>
#include <algorithm>

namespace Ime {

std::atomic<DWORD> LangBarButton::nextCookie = 0;

LangBarButton::LangBarButton(ComPtr<TextService> service, const GUID& guid, UINT commandId, const wchar_t* text, DWORD style):
    textService_(std::move(service)),
    commandId_(commandId),
    menu_(NULL),
    icon_(NULL),
    status_(0) {

    assert(textService_ && textService_->imeModule());

    info_.clsidService = textService_->imeModule()->textServiceClsid();
    info_.guidItem = guid;
    info_.dwStyle = style;
    info_.ulSort = 0;
    setText(text);
}

LangBarButton::~LangBarButton(void) {
    if (menu_) {
        ::DestroyMenu(menu_);
    }
}

const wchar_t* LangBarButton::text() const {
    return info_.szDescription;
}

void LangBarButton::setText(const wchar_t* text) {
    if (text && text[0] != '\0') {
        wcsncpy(info_.szDescription, text, TF_LBI_DESC_MAXLEN - 1);
        info_.szDescription[TF_LBI_DESC_MAXLEN - 1] = 0;
    }
    else {
        // NOTE: The language button text should NOT be empty.
        // Otherwise, when the button status or icon is changed after its creation,
        // the button will disappear temporarily in Windows 10 for unknown reason.
        // This can be considered a bug of Windows 10 and there does not seem to be a way to fix it.
        // So we need to avoid empty button text otherwise the language button won't work properly.
        // Here we use a space character to make the text non-empty to workaround the problem.
        wcscpy(info_.szDescription, L" ");
    }
    update(TF_LBI_TEXT);
}

void LangBarButton::setText(UINT stringId) {
    const wchar_t* str;
    int len = ::LoadStringW(textService_->imeModule()->hInstance(), stringId, (LPTSTR)&str, 0);
    if(str) {
        if (len > (TF_LBI_DESC_MAXLEN - 1)) {
            len = TF_LBI_DESC_MAXLEN - 1;
        }
        wcsncpy(info_.szDescription, str, len);
        info_.szDescription[len] = 0;
        update(TF_LBI_TEXT);
    }
}

// public methods
const std::wstring& LangBarButton::tooltip() const {
    return tooltip_;
}

void LangBarButton::setTooltip(std::wstring tooltip) {
    tooltip_ = std::move(tooltip);
    update(TF_LBI_TOOLTIP);
}

void LangBarButton::setTooltip(UINT tooltipId) {
    const wchar_t* str;
    //  If this parameter is 0, then lpBuffer receives a read-only pointer to the resource itself.
    auto len = ::LoadStringW(textService_->imeModule()->hInstance(), tooltipId, (LPTSTR)&str, 0);
    if(str) {
        tooltip_ = std::wstring(str, len);
        update(TF_LBI_TOOLTIP);
    }
}

HICON LangBarButton::icon() const {
    return icon_;
}

// The language button does not take owner ship of the icon
// That means, when the button is destroyed, it will not destroy
// the icon automatically.
void LangBarButton::setIcon(HICON icon) {
    icon_ = icon;
    update(TF_LBI_ICON);
}

void LangBarButton::setIcon(UINT iconId) {
    HICON icon = ::LoadIconW(textService_->imeModule()->hInstance(), (LPCTSTR)iconId);
    setIcon(icon);
}

UINT LangBarButton::commandId() const {
    return commandId_;
}

void LangBarButton::setCommandId(UINT id) {
    commandId_ = id;
}

HMENU LangBarButton::menu() const {
    return menu_;
}

// The button takes ownership of the menu and will delete it in destructor.
// FIXME: This is not consistent with setIcon(). Maybe change this later?
void LangBarButton::setMenu(HMENU menu) {
    if(menu_) {
        ::DestroyMenu(menu_);
    }
    menu_ = menu;
    // FIXME: how to handle toggle buttons?
    info_.dwStyle = menu ? TF_LBI_STYLE_BTN_MENU : TF_LBI_STYLE_BTN_BUTTON;
}

bool LangBarButton::enabled() const {
    return !(status_ & TF_LBI_STATUS_DISABLED);
}

void LangBarButton::setEnabled(bool enable) {
    if(enabled() != enable) {
        if (enable) {
            status_ &= ~TF_LBI_STATUS_DISABLED;
        }
        else {
            status_ |= TF_LBI_STATUS_DISABLED;
        }
        update(TF_LBI_STATUS);
    }
}

// need to create the button with TF_LBI_STYLE_BTN_TOGGLE style
bool LangBarButton::toggled() const {
    return (status_ & TF_LBI_STATUS_BTN_TOGGLED) ? true : false;
}

void LangBarButton::setToggled(bool toggle) {
    if(toggled() != toggle) {
        if (toggle) {
            status_ |= TF_LBI_STATUS_BTN_TOGGLED;
        }
        else {
            status_ &= ~TF_LBI_STATUS_BTN_TOGGLED;
        }
        update(TF_LBI_STATUS);
    }
}

DWORD LangBarButton::style() const {
    return info_.dwStyle;
}

void LangBarButton::setStyle(DWORD style) {
    info_.dwStyle = style;
}


// COM stuff

// ITfLangBarItem
STDMETHODIMP LangBarButton::GetInfo(TF_LANGBARITEMINFO *pInfo) {
    *pInfo = info_;
    return S_OK;
}

STDMETHODIMP LangBarButton::GetStatus(DWORD *pdwStatus) {
    *pdwStatus = status_;
    return S_OK;
}

STDMETHODIMP LangBarButton::Show(BOOL fShow) {
    return E_NOTIMPL;
}

STDMETHODIMP LangBarButton::GetTooltipString(BSTR *pbstrToolTip) {
    *pbstrToolTip = ::SysAllocString(tooltip_.c_str());
    return *pbstrToolTip ? S_OK : E_FAIL;
}

// ITfLangBarItemButton
STDMETHODIMP LangBarButton::OnClick(TfLBIClick click, POINT pt, const RECT *prcArea) {
    auto type = click == TF_LBI_CLK_RIGHT ? TextService::COMMAND_RIGHT_CLICK : TextService::COMMAND_LEFT_CLICK;
    textService_->onCommand(commandId_, type);
    return S_OK;
}

STDMETHODIMP LangBarButton::InitMenu(ITfMenu *pMenu) {
    if (!menu_) {
        return E_FAIL;
    }
    buildITfMenu(pMenu, menu_);
    return S_OK;
}

STDMETHODIMP LangBarButton::OnMenuSelect(UINT wID) {
    textService_->onCommand(wID, TextService::COMMAND_MENU);
    return S_OK;
}

STDMETHODIMP LangBarButton::GetIcon(HICON *phIcon) {
    // https://msdn.microsoft.com/zh-tw/library/windows/desktop/ms628718%28v=vs.85%29.aspx
    // The caller will delete the icon when it's no longer needed.
    // However, we might still need it. So let's return a copy here.
    *phIcon = (HICON)CopyImage(icon_, IMAGE_ICON, 0, 0, 0);
    return S_OK;
}

STDMETHODIMP LangBarButton::GetText(BSTR *pbstrText) {
    *pbstrText = ::SysAllocString(info_.szDescription);
    return *pbstrText ? S_OK : S_FALSE;
}

// ITfSource
STDMETHODIMP LangBarButton::AdviseSink(REFIID riid, IUnknown *punk, DWORD *pdwCookie) {
    if(IsEqualIID(riid, IID_ITfLangBarItemSink)) {
        if(auto langBarItemSink = ComPtr<ITfLangBarItemSink>::queryFrom(punk)) {
            *pdwCookie = nextCookie++;
            sinks_.emplace_back(*pdwCookie, langBarItemSink);
            return S_OK;
        }
        else {
            return E_NOINTERFACE;
        }
    }
    return CONNECT_E_CANNOTCONNECT;
}

STDMETHODIMP LangBarButton::UnadviseSink(DWORD dwCookie) {
    auto it = std::remove_if(sinks_.begin(), sinks_.end(),
        [=](const auto& item) {
            return item.first == dwCookie;
        }
    );
    if(it != sinks_.end()) {
        sinks_.erase(it, sinks_.end());
        return S_OK;
    }
    return CONNECT_E_NOCONNECTION;
}


// build ITfMenu according to the content of HMENU
void LangBarButton::buildITfMenu(ITfMenu* menu, HMENU templ) {
    int n = ::GetMenuItemCount(templ);
    for(int i = 0; i < n; ++i) {
        MENUITEMINFO mi;
        wchar_t textBuffer[256];
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        mi.dwTypeData = (LPTSTR)textBuffer;
        mi.cch = 255;
        mi.fMask = MIIM_FTYPE|MIIM_ID|MIIM_STATE|MIIM_STRING|MIIM_SUBMENU;
        if(::GetMenuItemInfoW(templ, i, TRUE, &mi)) {
            UINT flags = 0;
            wchar_t* text = nullptr;
            ULONG textLen = 0;
            ITfMenu* subMenu = NULL;
            ITfMenu** pSubMenu = NULL;
            if(mi.hSubMenu) { // has submenu
                pSubMenu = &subMenu;
                flags |= TF_LBMENUF_SUBMENU;
            }
            if(mi.fType == MFT_STRING) { // text item
                text = (wchar_t*)mi.dwTypeData;
                textLen = mi.cch;
            }
            else if(mi.fType == MFT_SEPARATOR) { // separator item
                flags |= TF_LBMENUF_SEPARATOR;
            }
            else { // other types are not supported
                continue;
            }

            if (mi.fState & MFS_CHECKED) { // checked
                flags |= TF_LBMENUF_CHECKED;
            }
            if (mi.fState & (MFS_GRAYED | MFS_DISABLED)) { // disabled
                flags |= TF_LBMENUF_GRAYED;
            }

            if(menu->AddMenuItem(mi.wID, flags, NULL, 0, text, textLen, pSubMenu) == S_OK) {
                if(subMenu) {
                    buildITfMenu(subMenu, mi.hSubMenu);
                    subMenu->Release();
                }
            }
        }
        else {
            DWORD error = ::GetLastError();
        }
    }
}

// call all sinks to generate update notifications
void LangBarButton::update(DWORD flags) {
    for(const auto& sinkPair: sinks_) {
        sinkPair.second->OnUpdate(flags);
    }
}

} // namespace Ime
