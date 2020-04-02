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
#include "SinkAdvice.h"
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
    const ComPtr<ImeModule>& imeModule() const {
        return module_;
    }

    const ComPtr<ITfThreadMgr>& threadMgr() const {
        return threadMgr_;
    }

    TfClientId clientId() const {
        return clientId_;
    }

    ComPtr<ITfContext> currentContext() const;

    bool isActivated() const {
        return (threadMgr() != nullptr);
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
    bool isComposing() const;

    // is keyboard disabled for the context (nullptr means current context)
    bool isKeyboardDisabled(ITfContext* context = nullptr) const;
    
    // is keyboard opened for the whole thread
    bool isKeyboardOpened() const;
    void setKeyboardOpen(bool open);

    bool isInsertionAllowed(EditSession* session) const;
    void startComposition(ITfContext* context);
    void endComposition(ITfContext* context);
    bool compositionRect(EditSession* session, RECT* rect) const;
    bool selectionRect(EditSession* session, RECT* rect) const;
    HWND compositionWindow(EditSession* session) const;

    std::wstring compositionString(EditSession* session) const;
    void setCompositionString(EditSession* session, const wchar_t* str, int len) const;
    void setCompositionCursor(EditSession* session, int pos) const;

    // compartment handling
    ComPtr<ITfCompartment> globalCompartment(const GUID& key) const;
    ComPtr<ITfCompartment> threadCompartment(const GUID& key) const;
    ComPtr<ITfCompartment> contextCompartment(const GUID& key, ITfContext* context = nullptr) const;

    DWORD globalCompartmentValue(const GUID& key) const;
    void setGlobalCompartmentValue(const GUID& key, DWORD value) const;

    DWORD threadCompartmentValue(const GUID& key) const;
    void setThreadCompartmentValue(const GUID& key, DWORD value) const;

    DWORD contextCompartmentValue(const GUID& key, ITfContext* context = nullptr) const;
    void setContextCompartmentValue(const GUID& key, DWORD value, ITfContext* context = nullptr) const;

    DWORD compartmentValue(ITfCompartment* compartment) const;
    void setCompartmentValue(ITfCompartment* compartment, DWORD value) const;

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
    STDMETHODIMP Activate(ITfThreadMgr *pThreadMgr, TfClientId tfClientId) override;
    STDMETHODIMP Deactivate() override;

    // ITfTextInputProcessorEx
    STDMETHODIMP ActivateEx(ITfThreadMgr *ptim, TfClientId tid, DWORD dwFlags) override;

    // ITfDisplayAttributeProvider
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) override;
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) override;

    // ITfThreadMgrEventSink
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr *pDocMgr) override;
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr *pDocMgr) override;
    STDMETHODIMP OnSetFocus(ITfDocumentMgr *pDocMgrFocus, ITfDocumentMgr *pDocMgrPrevFocus) override;
    STDMETHODIMP OnPushContext(ITfContext *pContext) override;
    STDMETHODIMP OnPopContext(ITfContext *pContext) override;

    // ITfTextEditSink
    STDMETHODIMP OnEndEdit(ITfContext *pContext, TfEditCookie ecReadOnly, ITfEditRecord *pEditRecord) override;

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL fForeground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pfEaten) override;
    STDMETHODIMP OnKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext *pContext, REFGUID rguid, BOOL *pfEaten) override;

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition *pComposition) override;

    // ITfCompartmentEventSink
    STDMETHODIMP OnChange(REFGUID rguid) override;

    // ITfLangBarEventSink 
    STDMETHODIMP OnSetFocus(DWORD dwThreadId) override;
    STDMETHODIMP OnThreadTerminate(DWORD dwThreadId) override;
    STDMETHODIMP OnThreadItemChange(DWORD dwThreadId) override;
    STDMETHODIMP OnModalInput(DWORD dwThreadId, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    STDMETHODIMP ShowFloating(DWORD dwFlags) override;
    STDMETHODIMP GetItemFloatingRect(DWORD dwThreadId, REFGUID rguid, RECT *prc) override;

    // ITfActiveLanguageProfileNotifySink
    STDMETHODIMP OnActivated(REFCLSID clsid, REFGUID guidProfile, BOOL fActivated) override;

protected:

    struct PreservedKey : public TF_PRESERVEDKEY {
        GUID guid;
    };

    void initKeyboardState();

    void installEventListeners();
    void uninstallEventListeners();

    void activateLanguageButtons();
    void deactivateLanguageButtons();

protected: // COM object should not be deleted directly. calling Release() instead.
    virtual ~TextService(void);

private:
    ComPtr<ImeModule> module_;
    ComPtr<ITfDisplayAttributeProvider> displayAttributeProvider_;
    ComPtr<ITfThreadMgr> threadMgr_;
    TfClientId clientId_;
    DWORD activateFlags_;
    bool isKeyboardOpened_;

    // event sink cookies
    SinkAdvice threadMgrEventSink_;
    SinkAdvice activateLanguageProfileNotifySink_;
    SinkAdvice keyboardOPenCloseSink_;
    SinkAdvice textEditSink_;
    DWORD langBarSinkCookie_;

    ComPtr<ITfComposition> composition_; // acquired when starting composition, released when ending composition
    ComPtr<ITfLangBarMgr> langBarMgr_;
    std::vector<ComPtr<LangBarButton>> langBarButtons_;
    std::vector<PreservedKey> preservedKeys_;
};

}

#endif  // IME_TEXT_SERVICE_H
