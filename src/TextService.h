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

#ifndef IME_TEXT_SERVICE_H
#define IME_TEXT_SERVICE_H

#include "libIME.h"
#include <msctf.h>
#include "EditSession.h"
#include "KeyEvent.h"
#include "ComPtr.h"
#include "DisplayAttributeInfo.h"
#include "DisplayAttributeProvider.h"
#include "ComObject.h"

#include <vector>
#include <list>
#include <string>

// for Windows 8 support
#ifndef TF_TMF_IMMERSIVEMODE // this is defined in Win 8 SDK
#define TF_TMF_IMMERSIVEMODE    0x40000000
#endif

namespace Ime {

class ImeModule;
class LangBarButton;

class TextService:
    public ComObject <
        // TSF interfaces
        ComInterface<ITfTextInputProcessorEx, ITfTextInputProcessor>,
        ComInterface<ITfDisplayAttributeProvider>,
        // event sinks
        ComInterface<ITfThreadMgrEventSink>,
        ComInterface<ITfTextEditSink>,
        ComInterface<ITfKeyEventSink>,
        ComInterface<ITfCompositionSink>,
        ComInterface<ITfCompartmentEventSink>,
        ComInterface<ITfLangBarEventSink>,
        ComInterface<ITfActiveLanguageProfileNotifySink>
    > {

public:

    enum CommandType { // used in onCommand()
        COMMAND_LEFT_CLICK,
        COMMAND_RIGHT_CLICK,
        COMMAND_MENU
    };

    TextService(ImeModule* module);

    // public methods
    ImeModule* imeModule() const;

    ITfThreadMgr* threadMgr() const;

    TfClientId clientId() const;

    ITfContext* currentContext();

    bool isActivated() const {
        return (threadMgr() != NULL);
    }

    DWORD activateFlags() const {
        return activateFlags_;
    }

    // running in Windows 8 app mode
    bool isImmersive() const {
        return (activateFlags_ & TF_TMF_IMMERSIVEMODE) != 0;
    }

    // alias of isImmersive()
    bool isMetroApp() const {
        return isImmersive();
    }

    // UI less mode is enabled (for ex: in fullscreen games)
    bool isUiLess() const {
        return (activateFlags_ & TF_TMF_UIELEMENTENABLEDONLY) != 0;
    }

    // is in console mode
    bool isConsole() const {
        return (activateFlags_ & TF_TMF_CONSOLE) != 0;
    }

    DWORD langBarStatus() const;

    // language bar buttons
    void addButton(LangBarButton* button);
    void removeButton(LangBarButton* button);

    // preserved keys
    void addPreservedKey(UINT keyCode, UINT modifiers, const GUID& guid);
    void removePreservedKey(const GUID& guid);

    // text composition handling
    bool isComposing();

    // is keyboard disabled for the context (NULL means current context)
    bool isKeyboardDisabled(ITfContext* context = NULL);
    
    // is keyboard opened for the whole thread
    bool isKeyboardOpened();
    void setKeyboardOpen(bool open);

    bool isInsertionAllowed(EditSession* session);
    void startComposition(ITfContext* context);
    void endComposition(ITfContext* context);
    bool compositionRect(EditSession* session, RECT* rect);
    bool selectionRect(EditSession* session, RECT* rect);
    HWND compositionWindow(EditSession* session);

    std::wstring compositionString(EditSession* session);
    void setCompositionString(EditSession* session, const wchar_t* str, int len);
    void setCompositionCursor(EditSession* session, int pos);

    // compartment handling
    ComPtr<ITfCompartment> globalCompartment(const GUID& key);
    ComPtr<ITfCompartment> threadCompartment(const GUID& key);
    ComPtr<ITfCompartment> contextCompartment(const GUID& key, ITfContext* context = NULL);

    DWORD globalCompartmentValue(const GUID& key);
    DWORD threadCompartmentValue(const GUID& key);
    DWORD contextCompartmentValue(const GUID& key, ITfContext* context = NULL);

    void setGlobalCompartmentValue(const GUID& key, DWORD value);
    void setThreadCompartmentValue(const GUID& key, DWORD value);
    void setContextCompartmentValue(const GUID& key, DWORD value, ITfContext* context = NULL);

    // manage sinks to global or thread compartment (context specific compartment is not used)
    void addCompartmentMonitor(const GUID key, bool isGlobal = false);
    void removeCompartmentMonitor(const GUID key);

    // virtual functions that IME implementors may need to override
    virtual void onActivate();
    virtual void onDeactivate();

    virtual void onSetFocus();
    virtual void onKillFocus();

    virtual bool filterKeyDown(KeyEvent& keyEvent);
    virtual bool onKeyDown(KeyEvent& keyEvent, EditSession* session);
    
    virtual bool filterKeyUp(KeyEvent& keyEvent);
    virtual bool onKeyUp(KeyEvent& keyEvent, EditSession* session);

    virtual bool onPreservedKey(const GUID& guid);

    // called when a language button or menu item is clicked
    virtual bool onCommand(UINT id, CommandType type);

    // called when a value in the global or thread compartment changed.
    virtual void onCompartmentChanged(const GUID& key);

    virtual void onLangBarStatusChanged(int newStatus);

    // called when the keyboard is opened or closed
    virtual void onKeyboardStatusChanged(bool opened);

    // called just before current composition is terminated for doing cleanup.
    // if forced is true, the composition is terminated by others, such as
    // the input focus is grabbed by another application.
    // if forced is false, the composition is terminated gracefully by endComposition().
    virtual void onCompositionTerminated(bool forced);

    // called when a language profile is activated (only useful for text services that supports multiple language profiles)
    virtual void onLangProfileActivated(REFGUID guidProfile);

    // called when a language profile is deactivated
    virtual void onLangProfileDeactivated(REFGUID guidProfile);

    // COM related stuff
public:
    friend class DisplayAttributeInfoEnum;

    // ITfTextInputProcessor
    STDMETHODIMP Activate(ITfThreadMgr *pThreadMgr, TfClientId tfClientId);
    STDMETHODIMP Deactivate();

    // ITfTextInputProcessorEx
    STDMETHODIMP ActivateEx(ITfThreadMgr *ptim, TfClientId tid, DWORD dwFlags);

    // ITfDisplayAttributeProvider
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) override;
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) override;

    // ITfThreadMgrEventSink
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr *pDocMgr);
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr *pDocMgr);
    STDMETHODIMP OnSetFocus(ITfDocumentMgr *pDocMgrFocus, ITfDocumentMgr *pDocMgrPrevFocus);
    STDMETHODIMP OnPushContext(ITfContext *pContext);
    STDMETHODIMP OnPopContext(ITfContext *pContext);

    // ITfTextEditSink
    STDMETHODIMP OnEndEdit(ITfContext *pContext, TfEditCookie ecReadOnly, ITfEditRecord *pEditRecord);

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL fForeground);
    STDMETHODIMP OnTestKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pfEaten);
    STDMETHODIMP OnKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pfEaten);
    STDMETHODIMP OnTestKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pfEaten);
    STDMETHODIMP OnKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pfEaten);
    STDMETHODIMP OnPreservedKey(ITfContext *pContext, REFGUID rguid, BOOL *pfEaten);

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition *pComposition);

    // ITfCompartmentEventSink
    STDMETHODIMP OnChange(REFGUID rguid);

    // ITfLangBarEventSink 
    STDMETHODIMP OnSetFocus(DWORD dwThreadId);
    STDMETHODIMP OnThreadTerminate(DWORD dwThreadId);
    STDMETHODIMP OnThreadItemChange(DWORD dwThreadId);
    STDMETHODIMP OnModalInput(DWORD dwThreadId, UINT uMsg, WPARAM wParam, LPARAM lParam);
    STDMETHODIMP ShowFloating(DWORD dwFlags);
    STDMETHODIMP GetItemFloatingRect(DWORD dwThreadId, REFGUID rguid, RECT *prc);

    // ITfActiveLanguageProfileNotifySink
    STDMETHODIMP OnActivated(REFCLSID clsid, REFGUID guidProfile, BOOL fActivated);

protected:

    HRESULT doStartCompositionEditSession(TfEditCookie cookie, ITfContext* context);
    HRESULT doEndCompositionEditSession(TfEditCookie cookie, ITfContext* context);

    struct PreservedKey : public TF_PRESERVEDKEY {
        GUID guid;
    };

    struct CompartmentMonitor {
        GUID guid;
        DWORD cookie;
        bool isGlobal;

        bool operator == (const GUID& other) const {
            return bool(::IsEqualGUID(guid, other));
        }
    };

protected: // COM object should not be deleted directly. calling Release() instead.
    virtual ~TextService(void);

private:
    ComPtr<ImeModule> module_;
    ComPtr< ITfDisplayAttributeProvider> displayAttributeProvider_;
    ComPtr<ITfThreadMgr> threadMgr_;
    TfClientId clientId_;
    DWORD activateFlags_;
    bool isKeyboardOpened_;

    // event sink cookies
    DWORD threadMgrEventSinkCookie_;
    DWORD textEditSinkCookie_;
    DWORD compositionSinkCookie_;
    DWORD keyboardOpenEventSinkCookie_;
    DWORD globalCompartmentEventSinkCookie_;
    DWORD langBarSinkCookie_;
    DWORD activateLanguageProfileNotifySinkCookie_;

    ITfComposition* composition_; // acquired when starting composition, released when ending composition
    ComPtr<ITfLangBarMgr> langBarMgr_;
    std::vector<ComPtr<LangBarButton>> langBarButtons_;
    std::vector<PreservedKey> preservedKeys_;
    std::vector<CompartmentMonitor> compartmentMonitors_;
};

}

#endif  // IME_TEXT_SERVICE_H
