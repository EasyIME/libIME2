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

#include "TextService.h"
#include "EditSession.h"
#include "CandidateWindow.h"
#include "LangBarButton.h"
#include "DisplayAttributeInfoEnum.h"
#include "ImeModule.h"

#include <assert.h>
#include <string>
#include <algorithm>

using namespace std;

namespace Ime {

TextService::TextService(ImeModule* module):
    module_(module),
    displayAttributeProvider_{ComPtr<DisplayAttributeProvider>::make(module)},
    threadMgr_(NULL),
    clientId_(TF_CLIENTID_NULL),
    activateFlags_(0),
    isKeyboardOpened_(false),
    langBarSinkCookie_(TF_INVALID_COOKIE) {

}

TextService::~TextService(void) {
    if(langBarMgr_) {
        langBarMgr_->UnadviseEventSink(langBarSinkCookie_);
    }
}

// public methods

// language bar
DWORD TextService::langBarStatus() const {
    if(langBarMgr_) {
        DWORD status;
        if(langBarMgr_->GetShowFloatingStatus(&status) == S_OK) {
            return status;
        }
    }
    return 0;
}

void TextService::addButton(LangBarButton* button) {
    if(button) {
        langBarButtons_.emplace_back(button);
        if(isActivated()) {
            if(auto langBarItemMgr = threadMgr_.query<ITfLangBarItemMgr>()) {
                langBarItemMgr->AddItem(button);
            }
        }
    }
}

void TextService::removeButton(LangBarButton* button) {
    if(button) {
        auto it = find(langBarButtons_.begin(), langBarButtons_.end(), button);
        if(it != langBarButtons_.end()) {
            if(isActivated()) {
                if(auto langBarItemMgr = threadMgr_.query<ITfLangBarItemMgr>()) {
                    langBarItemMgr->RemoveItem(button);
                }
            }
            langBarButtons_.erase(it);
        }
    }
}

// preserved key
void TextService::addPreservedKey(UINT keyCode, UINT modifiers, const GUID& guid) {
    PreservedKey preservedKey;
    preservedKey.guid = guid;
    preservedKey.uVKey = keyCode;
    preservedKey.uModifiers = modifiers;
    preservedKeys_.push_back(preservedKey);
    if(threadMgr_) { // our text service is activated
        if (auto keystrokeMgr = threadMgr_.query<ITfKeystrokeMgr>()) {
            keystrokeMgr->PreserveKey(clientId_, guid, &preservedKey, NULL, 0);
        }
    }
}

void TextService::removePreservedKey(const GUID& guid) {
    auto it = std::find_if(preservedKeys_.begin(), preservedKeys_.end(),
        [&](const auto& preservedKey) {
            return ::IsEqualIID(preservedKey.guid, guid);
        }
    );
    if (it != preservedKeys_.end()) {
        if (auto keystrokeMgr = threadMgr_.query<ITfKeystrokeMgr>()) {
            auto& preservedKey = *it;
            keystrokeMgr->UnpreserveKey(preservedKey.guid, &preservedKey);
        }
        preservedKeys_.erase(it);
    }
}


// text composition

bool TextService::isComposing() const {
    return (composition_ != nullptr);
}

// is keyboard disabled for the context (NULL means current context)
bool TextService::isKeyboardDisabled(ITfContext* context) const {
    return (contextCompartmentValue(GUID_COMPARTMENT_KEYBOARD_DISABLED, context)
        || contextCompartmentValue(GUID_COMPARTMENT_EMPTYCONTEXT, context));
}

// is keyboard opened for the whole thread
bool TextService::isKeyboardOpened() const {
    return isKeyboardOpened_;
}

void TextService::setKeyboardOpen(bool open) {
    if(open != isKeyboardOpened_) {
        // The event handler triggered by the compartment value will update isKeyboardOpened_.
        setThreadCompartmentValue(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, DWORD(open));
    }
}

// check if current insertion point is in the range of composition.
// if not in range, insertion is now allowed
bool TextService::isInsertionAllowed(EditSession* session) const {
    TfEditCookie cookie = session->editCookie();
    ULONG selectionNum;
    if(isComposing()) {
        TF_SELECTION selection;
        if(session->context()->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &selectionNum) == S_OK) {
            ComPtr<ITfRange> compositionRange;
            if(composition_->GetRange(&compositionRange) == S_OK) {
                bool allowed = false;
                // check if current selection is covered by composition range
                LONG compareResult1;
                LONG compareResult2;
                if(selection.range->CompareStart(cookie, compositionRange, TF_ANCHOR_START, &compareResult1) == S_OK
                    && selection.range->CompareStart(cookie, compositionRange, TF_ANCHOR_END, &compareResult2) == S_OK) {
                    if(compareResult1 == -1 && compareResult2 == +1)
                        allowed = true;
                }
            }
            selection.range->Release();
        }
    }
    return false;
}

void TextService::startComposition(ITfContext* context) {
    assert(context);
    HRESULT sessionResult;
    auto editSession = ComPtr<EditSession>::make(
        context,
        [=](EditSession* session, TfEditCookie cookie) {
            if (auto contextComposition = ComPtr<ITfContextComposition>::queryFrom(context)) {
                // get current insertion point in the current context
                ComPtr<ITfRange> range;
                if (auto insertAtSelection = ComPtr<ITfInsertAtSelection>::queryFrom(context)) {
                    // get current selection range & insertion position (query only, did not insert any text)
                    insertAtSelection->InsertTextAtSelection(cookie, TF_IAS_QUERYONLY, NULL, 0, &range);
                }

                if (range) {
                    composition_ = nullptr;
                    if (contextComposition->StartComposition(cookie, range, (ITfCompositionSink*)this, &composition_) == S_OK) {
                        // according to the official TSF samples, we need to reset the current
                        // selection here. (maybe the range is altered by StartComposition()?
                        TF_SELECTION selection;
                        selection.range = range;
                        selection.style.ase = TF_AE_NONE;
                        selection.style.fInterimChar = FALSE;
                        context->SetSelection(cookie, 1, &selection);
                    }
                }
            }
        }
    );
    context->RequestEditSession(clientId_, editSession, TF_ES_SYNC|TF_ES_READWRITE, &sessionResult);
}

void TextService::endComposition(ITfContext* context) {
    assert(context);
    HRESULT sessionResult;
    auto editSession = ComPtr<EditSession>::make(
        context,
        [=](EditSession* session, TfEditCookie cookie) {
            if (composition_) {
                // move current insertion point to end of the composition string
                ComPtr<ITfRange> compositionRange;
                if (composition_->GetRange(&compositionRange) == S_OK) {
                    // clear display attribute for the composition range
                    ComPtr<ITfProperty> dispAttrProp;
                    if (context->GetProperty(GUID_PROP_ATTRIBUTE, &dispAttrProp) == S_OK) {
                        dispAttrProp->Clear(cookie, compositionRange);
                    }

                    TF_SELECTION selection;
                    ULONG selectionNum;
                    if (context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &selectionNum) == S_OK) {
                        selection.range->ShiftEndToRange(cookie, compositionRange, TF_ANCHOR_END);
                        selection.range->Collapse(cookie, TF_ANCHOR_END);
                        context->SetSelection(cookie, 1, &selection);
                        selection.range->Release();
                    }
                }
                // end composition and clean up
                composition_->EndComposition(cookie);
                // do some cleanup in the derived class here
                onCompositionTerminated(false);
                composition_ = nullptr;
            }
        }
    );
    context->RequestEditSession(clientId_, editSession, TF_ES_SYNC|TF_ES_READWRITE, &sessionResult);
}

std::wstring TextService::compositionString(EditSession* session) const {
    std::wstring result;
    if (composition_) {
        ComPtr<ITfRange> compositionRange;
        if (composition_->GetRange(&compositionRange) == S_OK) {
            auto rangeAcp = compositionRange.query<ITfRangeACP>();
            if (rangeAcp) {
                LONG anchor, bufLen;
                rangeAcp->GetExtent(&anchor, &bufLen);  // get length of the text.
                auto buf = std::make_unique<wchar_t[]>(size_t(bufLen) + 1);

                ULONG textLen = 0;
                if (compositionRange->GetText(session->editCookie(), 0, buf.get(), bufLen, &textLen) == S_OK) {
                    buf[textLen] = '\0';
                    result = buf.get();
                }
            }
        }
    }
    return result;
}

void TextService::setCompositionString(EditSession* session, const wchar_t* str, int len) const {
    ITfContext* context = session->context();
    if(context) {
        TfEditCookie editCookie = session->editCookie();
        TF_SELECTION selection;
        ULONG selectionNum;
        // get current selection/insertion point
        if(context->GetSelection(editCookie, TF_DEFAULT_SELECTION, 1, &selection, &selectionNum) == S_OK) {
            ComPtr<ITfRange> compositionRange;
            if(composition_->GetRange(&compositionRange) == S_OK) {
                bool selPosInComposition = true;
                // if current insertion point is not covered by composition, we cannot insert text here.
                if(selPosInComposition) {
                    // replace context of composion area with the new string.
                    compositionRange->SetText(editCookie, TF_ST_CORRECTION, str, len);

                    // move the insertion point to end of the composition string
                    selection.range->Collapse(editCookie, TF_ANCHOR_END);
                    context->SetSelection(editCookie, 1, &selection);
                }

                // set display attribute to the composition range
                ComPtr<ITfProperty> dispAttrProp;
                if(context->GetProperty(GUID_PROP_ATTRIBUTE, &dispAttrProp) == S_OK) {
                    VARIANT val;
                    val.vt = VT_I4;
                    val.lVal = module_->inputAttrib()->atom();
                    dispAttrProp->SetValue(editCookie, compositionRange, &val);
                }
            }
            selection.range->Release();
        }
    }
}

// set cursor position in the composition area
// 0 means the start pos of composition string
void TextService::setCompositionCursor(EditSession* session, int pos) const {
    TF_SELECTION selection;
    ULONG selectionNum;
    // get current selection
    if(session->context()->GetSelection(session->editCookie(), TF_DEFAULT_SELECTION, 1, &selection, &selectionNum) == S_OK) {
        // get composition range
        ComPtr<ITfRange> compositionRange;
        if(composition_->GetRange(&compositionRange) == S_OK) {
            // make the start of selectionRange the same as that of compositionRange
            selection.range->ShiftStartToRange(session->editCookie(), compositionRange, TF_ANCHOR_START);
            selection.range->Collapse(session->editCookie(), TF_ANCHOR_START);
            LONG moved;
            // move the start anchor to right
            selection.range->ShiftStart(session->editCookie(), (LONG)pos, &moved, NULL);
            selection.range->Collapse(session->editCookie(), TF_ANCHOR_START);
            // set the new selection to the context
            session->context()->SetSelection(session->editCookie(), 1, &selection);
        }
        selection.range->Release();
    }
}

// compartment handling
ComPtr<ITfCompartment> TextService::globalCompartment(const GUID& key) const {
    ComPtr<ITfCompartment> compartment;
    auto threadMgr = threadMgr_;
    if (threadMgr == nullptr) {
        // if we don't have a thread manager (this is possible when we try to access
        // a global compartment while the text service is not activated)
        ::CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void**)&threadMgr);
    }
    if(threadMgr) {
        ComPtr<ITfCompartmentMgr> compartmentMgr;
        if(threadMgr_->GetGlobalCompartment(&compartmentMgr) == S_OK) {
            compartmentMgr->GetCompartment(key, &compartment);
        }
    }
    return compartment;
}

ComPtr<ITfCompartment> TextService::threadCompartment(const GUID& key) const {
    if(threadMgr_) {
        auto compartmentMgr = threadMgr_.query<ITfCompartmentMgr>();
        if(compartmentMgr) {
            ComPtr<ITfCompartment> compartment;
            compartmentMgr->GetCompartment(key, &compartment);
            return compartment;
        }
    }
    return nullptr;
}

ComPtr<ITfCompartment> TextService::contextCompartment(const GUID& key, ITfContext* context) const {
    ComPtr<ITfContext> curContext;
    if(!context) {
        curContext = currentContext();
        context = curContext;
    }
    if(context) {
        auto compartmentMgr = ComPtr<ITfCompartmentMgr>::queryFrom(context);
        if(compartmentMgr) {
            ComPtr<ITfCompartment> compartment;
            compartmentMgr->GetCompartment(key, &compartment);
            return compartment;
        }
    }
    return nullptr;
}

DWORD TextService::globalCompartmentValue(const GUID& key) const{
    if (auto compartment = globalCompartment(key)) {
        return compartmentValue(compartment);
    }
    return 0;
}

void TextService::setGlobalCompartmentValue(const GUID& key, DWORD value) const {
    if (auto compartment = globalCompartment(key)) {
        setCompartmentValue(compartment, value);
    };
}

DWORD TextService::contextCompartmentValue(const GUID& key, ITfContext* context) const {
    if (auto compartment = contextCompartment(key)) {
        return compartmentValue(compartment);
    }
    return 0;
}

void TextService::setContextCompartmentValue(const GUID& key, DWORD value, ITfContext* context) const {
    if (auto compartment = contextCompartment(key, context)) {
        setCompartmentValue(compartment, value);
    }
}

DWORD TextService::threadCompartmentValue(const GUID& key) const {
    if (auto compartment = threadCompartment(key)) {
        return compartmentValue(compartment);
    }
    return 0;
}

void TextService::setThreadCompartmentValue(const GUID& key, DWORD value) const {
    if (auto compartment = threadCompartment(key)) {
        setCompartmentValue(compartment, value);
    }
}

DWORD TextService::compartmentValue(ITfCompartment* compartment) const {
    VARIANT var;
    if (compartment->GetValue(&var) == S_OK && var.vt == VT_I4) {
        return (DWORD)var.lVal;
    }
    return 0;
}

void TextService::setCompartmentValue(ITfCompartment* compartment, DWORD value) const {
    VARIANT var;
    ::VariantInit(&var);
    var.vt = VT_I4;
    var.lVal = value;
    compartment->SetValue(clientId_, &var);
}

// virtual
void TextService::onActivate() {
}

// virtual
void TextService::onDeactivate() {
}

// virtual
bool TextService::filterKeyDown(KeyEvent& keyEvent) {
    return false;
}

// virtual
bool TextService::onKeyDown(KeyEvent& keyEvent, EditSession* session) {
    return false;
}

// virtual
bool TextService::filterKeyUp(KeyEvent& keyEvent) {
    return false;
}

// virtual
bool TextService::onKeyUp(KeyEvent& keyEvent, EditSession* session) {
    return false;
}

// virtual
bool TextService::onPreservedKey(const GUID& guid) {
    return false;
}

// virtual
void TextService::onSetFocus() {
}

// virtual
void TextService::onKillFocus() {
}

bool TextService::onCommand(UINT id, CommandType type) {
    return false;
}

// virtual
void TextService::onCompartmentChanged(const GUID& key) {
    // keyboard status changed, this is threadMgr specific
    // See explanations on TSF aware blog:
    // http://blogs.msdn.com/b/tsfaware/archive/2007/05/30/what-is-a-keyboard.aspx
    if(::IsEqualGUID(key, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)) {
        isKeyboardOpened_ = threadCompartmentValue(key) ? true : false;
        onKeyboardStatusChanged(isKeyboardOpened_);
    }
}

// virtual
void TextService::onLangBarStatusChanged(int newStatus) {
}

// called when the keyboard is opened or closed
// virtual
void TextService::onKeyboardStatusChanged(bool opened) {
}

// called just before current composition is terminated for doing cleanup.
// if forced is true, the composition is terminated by others, such as
// the input focus is grabbed by another application.
// if forced is false, the composition is terminated gracefully by endComposition().
// virtual
void TextService::onCompositionTerminated(bool forced) {
}

// called when a language profile is activated (only useful for text services that supports multiple language profiles)
void TextService::onLangProfileActivated(REFGUID guidProfile) {
}

// called when a language profile is deactivated
void TextService::onLangProfileDeactivated(REFGUID guidProfile) {
}

void TextService::initKeyboardState() {
    // get current keyboard state
    isKeyboardOpened_ = threadCompartmentValue(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE) != 0;
    // FIXME: under Windows 7, it seems that the keyboard is closed every time
    // our text service is activated. The value in the compartment is always empty. :-(
    // So, we open the keyboard manually here, but I'm not sure if this is the correct behavior.
    if (!isKeyboardOpened_) {
        setKeyboardOpen(true);
    }
}

void TextService::installEventListeners() {
    // ITfThreadMgrEventSink, ITfActiveLanguageProfileNotifySink, and ITfTextEditSink
    if (auto source = threadMgr_.query<ITfSource>()) {
        threadMgrEventSink_ = SinkAdvice{ source, IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this) };
        activateLanguageProfileNotifySink_ = SinkAdvice{ source, IID_ITfActiveLanguageProfileNotifySink, static_cast<ITfActiveLanguageProfileNotifySink*>(this) };
        textEditSink_ = SinkAdvice{ source, IID_ITfTextEditSink, static_cast<ITfTextEditSink*>(this) };
    }

    // ITfKeyEventSink
    if (auto keystrokeMgr = threadMgr_.query<ITfKeystrokeMgr>()) {
        keystrokeMgr->AdviseKeyEventSink(clientId_, (ITfKeyEventSink*)this, TRUE);

        // register preserved keys
        for (const auto& preservedKey : preservedKeys_) {
            keystrokeMgr->PreserveKey(clientId_, preservedKey.guid, &preservedKey, NULL, 0);
        }
    }

    // Keyboard open status change.
    if (auto source = threadCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE).query<ITfSource>()) {
        keyboardOPenCloseSink_ = SinkAdvice(source, IID_ITfCompartmentEventSink, (ITfCompartmentEventSink*)this);
    }
}

void TextService::uninstallEventListeners() {
    // ITfThreadMgrEventSink, ITfActiveLanguageProfileNotifySink, and ITfTextEditSink
    threadMgrEventSink_.unadvise();
    activateLanguageProfileNotifySink_.unadvise();
    textEditSink_.unadvise();

    // ITfKeyEventSink
    if (auto keystrokeMgr = threadMgr_.query<ITfKeystrokeMgr>()) {
        keystrokeMgr->UnadviseKeyEventSink(clientId_);
        // unregister preserved keys
        for (const auto& preservedKey : preservedKeys_) {
            keystrokeMgr->UnpreserveKey(preservedKey.guid, &preservedKey);
        }
    }

    // Keyboard open status change.
    keyboardOPenCloseSink_.unadvise();
}

void TextService::activateLanguageButtons() {
    ::CoCreateInstance(CLSID_TF_LangBarMgr, NULL, CLSCTX_INPROC_SERVER,
        IID_ITfLangBarMgr, (void**)&langBarMgr_);
    if (langBarMgr_) {
        langBarMgr_->AdviseEventSink(this, NULL, 0, &langBarSinkCookie_);
    }
    // Note: language bar has no effects in Win 8 immersive mode
    if (!langBarButtons_.empty()) {
        if (auto langBarItemMgr = threadMgr_.query<ITfLangBarItemMgr>()) {
            for (auto& button : langBarButtons_) {
                langBarItemMgr->AddItem(button);
            }
        }
    }
}

void TextService::deactivateLanguageButtons() {
    if (!langBarButtons_.empty()) {
        if (auto langBarItemMgr = threadMgr_.query<ITfLangBarItemMgr>()) {
            for (auto& button : langBarButtons_) {
                langBarItemMgr->RemoveItem(button);
            }
        }
    }
    if (langBarMgr_) {
        langBarMgr_->UnadviseEventSink(langBarSinkCookie_);
        langBarSinkCookie_ = TF_INVALID_COOKIE;
        langBarMgr_ = NULL;
    }
}

// COM stuff

// ITfTextInputProcessor
STDMETHODIMP TextService::Activate(ITfThreadMgr *pThreadMgr, TfClientId tfClientId) {
    // store tsf manager & client id
    threadMgr_ = pThreadMgr;
    clientId_ = tfClientId;

    activateFlags_ = 0;
    if(auto threadMgrEx = threadMgr_.query<ITfThreadMgrEx>()) {
        threadMgrEx->GetActiveFlags(&activateFlags_);
    }

    installEventListeners();
    initKeyboardState();
    activateLanguageButtons();

    onActivate();
    return S_OK;
}

STDMETHODIMP TextService::Deactivate() {
    // terminate composition properly
    if(isComposing()) {
        if(auto context = currentContext()) {
            endComposition(context);
        }
    }

    onDeactivate();

    deactivateLanguageButtons();
    uninstallEventListeners();

    threadMgr_ = nullptr;
    clientId_ = TF_CLIENTID_NULL;
    activateFlags_ = 0;
    return S_OK;
}

// ITfTextInputProcessorEx
STDMETHODIMP TextService::ActivateEx(ITfThreadMgr *ptim, TfClientId tid, DWORD dwFlags) {
    Activate(ptim, tid);
    return S_OK;
}

// ITfDisplayAttributeProvider
STDMETHODIMP TextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) {
    return displayAttributeProvider_->EnumDisplayAttributeInfo(ppEnum);
}

STDMETHODIMP TextService::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) {
    return displayAttributeProvider_->GetDisplayAttributeInfo(guid, ppInfo);
}

// ITfThreadMgrEventSink
STDMETHODIMP TextService::OnInitDocumentMgr(ITfDocumentMgr *pDocMgr) {
    return S_OK;
}

STDMETHODIMP TextService::OnUninitDocumentMgr(ITfDocumentMgr *pDocMgr) {
    return S_OK;
}

STDMETHODIMP TextService::OnSetFocus(ITfDocumentMgr *pDocMgrFocus, ITfDocumentMgr *pDocMgrPrevFocus) {
    return S_OK;
}

STDMETHODIMP TextService::OnPushContext(ITfContext *pContext) {
    return S_OK;
}

STDMETHODIMP TextService::OnPopContext(ITfContext *pContext) {
    return S_OK;
}


// ITfTextEditSink
STDMETHODIMP TextService::OnEndEdit(ITfContext *pContext, TfEditCookie ecReadOnly, ITfEditRecord *pEditRecord) {
    // This method is called by the TSF whenever an edit operation ends.
    // It's possible for a document to have multiple composition strings at the
    // same time and it's possible for other text services to edit the same
    // document. Though such a complicated senario rarely exist, it indeed happen.

    // NOTE: I don't really know why this is needed and tests yielded no obvious effect
    // of this piece of code, but from MS TSF samples, this is needed.
    BOOL selChanged;
    if(pEditRecord->GetSelectionStatus(&selChanged) == S_OK) {
        if(selChanged && isComposing()) {
            // we need to check if current selection is in our composition string.
            // if after others' editing the selection (insertion point) has been changed and
            // fell outside our composition area, terminate the composition.
            TF_SELECTION selection;
            ULONG selectionNum;
            if(pContext->GetSelection(ecReadOnly, TF_DEFAULT_SELECTION, 1, &selection, &selectionNum) == S_OK) {
                ComPtr<ITfRange> compRange;
                if(composition_->GetRange(&compRange) == S_OK) {
                    // check if two ranges overlaps
                    // check if current selection is covered by composition range
                    LONG compareResult1;
                    LONG compareResult2;
                    if(compRange->CompareStart(ecReadOnly, selection.range, TF_ANCHOR_START, &compareResult1) == S_OK
                        && compRange->CompareEnd(ecReadOnly, selection.range, TF_ANCHOR_END, &compareResult2) == S_OK) {
                        if(compareResult1 == +1 || compareResult2 == -1) {
                            // the selection is not entirely in composion
                            // end compositon here
                            endComposition(pContext);
                        }
                    }
                }
                selection.range->Release();
            }
        }
    }

    return S_OK;
}


// ITfKeyEventSink
STDMETHODIMP TextService::OnSetFocus(BOOL fForeground) {
    if (fForeground) {
        onSetFocus();
    }
    else {
        onKillFocus();
    }
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pfEaten) {
    if (isKeyboardDisabled(pContext) || !isKeyboardOpened()) {
        *pfEaten = FALSE;
    }
    else {
        KeyEvent keyEvent(WM_KEYDOWN, wParam, lParam);
        *pfEaten = (BOOL)filterKeyDown(keyEvent);
    }
    return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pfEaten) {
    // Some applications do not trigger OnTestKeyDown()
    // So we need to test it again here! Windows TSF sucks!
    if (isKeyboardDisabled(pContext) || !isKeyboardOpened()) {
        *pfEaten = FALSE;
    }
    else {
        KeyEvent keyEvent(WM_KEYDOWN, wParam, lParam);
        *pfEaten = (BOOL)filterKeyDown(keyEvent);
        if(*pfEaten) { // we want to eat the key
            HRESULT sessionResult;
            // ask TSF for an edit session. If editing is approved by TSF,
            // KeyEditSession::DoEditSession will be called, which in turns
            // call back to TextService::doKeyEditSession().
            // So the real key handling is relayed to TextService::doKeyEditSession().
            auto session = ComPtr<EditSession>::make(
                pContext,
                [&](EditSession* session, TfEditCookie cookie) {
                    *pfEaten = onKeyDown(keyEvent, session);
                }
            );
            // We use TF_ES_SYNC here, so the request becomes synchronus and blocking.
            // KeyEditSession::DoEditSession() and TextService::doKeyEditSession() will be
            // called before RequestEditSession() returns.
            pContext->RequestEditSession(clientId_, session, TF_ES_SYNC|TF_ES_READWRITE, &sessionResult);
        }
    }
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pfEaten) {
    if (isKeyboardDisabled(pContext) || !isKeyboardOpened()) {
        *pfEaten = FALSE;
    }
    else {
        KeyEvent keyEvent(WM_KEYDOWN, wParam, lParam);
        *pfEaten = (BOOL)filterKeyUp(keyEvent);
    }
    return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pfEaten) {
    // Some applications do not trigger OnTestKeyDown()
    // So we need to test it again here! Windows TSF sucks!
    if (isKeyboardDisabled(pContext) || !isKeyboardOpened()) {
        *pfEaten = FALSE;
    }
    else {
        KeyEvent keyEvent(WM_KEYUP, wParam, lParam);
        *pfEaten = (BOOL)filterKeyUp(keyEvent);
        if(*pfEaten) {
            HRESULT sessionResult;
            auto session = ComPtr<EditSession>::make(
                pContext,
                [&](EditSession* session, TfEditCookie cookie) {
                    *pfEaten = onKeyUp(keyEvent, session);
                }
            );
            pContext->RequestEditSession(clientId_, session, TF_ES_SYNC|TF_ES_READWRITE, &sessionResult);
        }
    }
    return S_OK;
}

STDMETHODIMP TextService::OnPreservedKey(ITfContext *pContext, REFGUID rguid, BOOL *pfEaten) {
    *pfEaten = (BOOL)onPreservedKey(rguid);
    return S_OK;
}


// ITfCompositionSink
STDMETHODIMP TextService::OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition *pComposition) {
    // This is called by TSF when our composition is terminated by others.
    // For example, when the user click on another text editor and the input focus is 
    // grabbed by others, we're ``forced'' to terminate current composition.
    // If we end the composition by calling ITfComposition::EndComposition() ourselves,
    // this event is not triggered.
    onCompositionTerminated(true);
    composition_ = nullptr;
    return S_OK;
}

// ITfCompartmentEventSink
STDMETHODIMP TextService::OnChange(REFGUID rguid) {
    // The TSF ITfCompartment is kind of key-value storage
    // It uses GUIDs as keys and stores integer and string values.
    // Global compartment is a storage for cross-process key-value pairs.
    // However, it only handles integers. String values cannot be stored.
    // The thread manager specific compartment, however, handles strings.
    // Every value stored in the storage has an key, which is a GUID.
    // global keyboard states and some other values are in the compartments
    // So we need to monitor for their changes and do some handling.

    // For more detailed introduction, see TSF aware blog:
    // http://blogs.msdn.com/b/tsfaware/archive/2007/05/30/what-is-a-keyboard.aspx

    onCompartmentChanged(rguid);
    return S_OK;
}

// ITfLangBarEventSink 
STDMETHODIMP TextService::OnSetFocus(DWORD dwThreadId) {
    return E_NOTIMPL;
}

STDMETHODIMP TextService::OnThreadTerminate(DWORD dwThreadId) {
    return E_NOTIMPL;
}

STDMETHODIMP TextService::OnThreadItemChange(DWORD dwThreadId) {
    return E_NOTIMPL;
}

STDMETHODIMP TextService::OnModalInput(DWORD dwThreadId, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    return E_NOTIMPL;
}

STDMETHODIMP TextService::ShowFloating(DWORD dwFlags) {
    onLangBarStatusChanged(dwFlags);
    return S_OK;
}

STDMETHODIMP TextService::GetItemFloatingRect(DWORD dwThreadId, REFGUID rguid, RECT *prc) {
    return E_NOTIMPL;
}


// ITfActiveLanguageProfileNotifySink
STDMETHODIMP TextService::OnActivated(REFCLSID clsid, REFGUID guidProfile, BOOL fActivated) {
    // we only support one text service, so clsid must be the same as that of our text service.
    // otherwise it's not the notification for our text service, just ignore the event.
    if(clsid == module_->textServiceClsid()) {
        if (fActivated) {
            onLangProfileActivated(guidProfile);
        }
        else {
            onLangProfileDeactivated(guidProfile);
        }
    }
    return S_OK;
}

ComPtr<ITfContext> TextService::currentContext() const {
    ComPtr<ITfContext> context;
    ComPtr<ITfDocumentMgr>  docMgr;
    if(threadMgr_->GetFocus(&docMgr) == S_OK) {
        docMgr->GetTop(&context);
    }
    return context;
}

bool TextService::compositionRect(EditSession* session, RECT* rect) const {
    bool ret = false;
    if(isComposing()) {
        ComPtr<ITfContextView> view;
        if(session->context()->GetActiveView(&view) == S_OK) {
            BOOL clipped;
            ComPtr<ITfRange> range;
            if(composition_->GetRange(&range) == S_OK) {
                if(view->GetTextExt(session->editCookie(), range, rect, &clipped) == S_OK)
                    ret = true;
            }
        }
    }
    return ret;
}

bool TextService::selectionRect(EditSession* session, RECT* rect) const {
    bool ret = false;
    if(isComposing()) {
        ComPtr<ITfContextView> view;
        if(session->context()->GetActiveView(&view) == S_OK) {
            BOOL clipped;
            TF_SELECTION selection;
            ULONG selectionNum;
            if(session->context()->GetSelection(session->editCookie(), TF_DEFAULT_SELECTION, 1, &selection, &selectionNum) == S_OK ) {
                if(view->GetTextExt(session->editCookie(), selection.range, rect, &clipped) == S_OK)
                    ret = true;
                selection.range->Release();
            }
        }
    }
    return ret;
}

HWND TextService::compositionWindow(EditSession* session) const {
    HWND hwnd = NULL;
    ComPtr<ITfContextView> view;
    if(session->context()->GetActiveView(&view) == S_OK) {
        // get current composition window
        view->GetWnd(&hwnd);
    }
    if (hwnd == NULL)
        hwnd = ::GetFocus();
    return hwnd;
}

} // namespace Ime

