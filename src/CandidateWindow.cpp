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

#include "CandidateWindow.h"
#include "DrawUtils.h"
#include "TextService.h"
#include "EditSession.h"

#include <algorithm>
#include <cassert>

#include <tchar.h>
#include <windows.h>

using namespace std;

namespace Ime {

static COLORREF blendColor(COLORREF a, COLORREF b, int percentB) {
    int percentA = 100 - percentB;
    return RGB(
        (GetRValue(a) * percentA + GetRValue(b) * percentB) / 100,
        (GetGValue(a) * percentA + GetGValue(b) * percentB) / 100,
        (GetBValue(a) * percentA + GetBValue(b) * percentB) / 100
    );
}

static int colorLuma(COLORREF color) {
    return (GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114) / 1000;
}

static int colorContrast(COLORREF a, COLORREF b) {
    int diff = colorLuma(a) - colorLuma(b);
    return diff < 0 ? -diff : diff;
}

static COLORREF readableTextOnColor(COLORREF bg, COLORREF preferred, COLORREF alternate) {
    COLORREF dark = RGB(17, 24, 39);
    COLORREF light = RGB(248, 250, 252);
    COLORREF result = preferred;
    int resultContrast = colorContrast(bg, result);
    int alternateContrast = colorContrast(bg, alternate);

    if (alternateContrast > resultContrast) {
        result = alternate;
        resultContrast = alternateContrast;
    }

    int darkContrast = colorContrast(bg, dark);
    int lightContrast = colorContrast(bg, light);
    if (resultContrast < 72 && darkContrast > resultContrast) {
        result = dark;
        resultContrast = darkContrast;
    }
    if (resultContrast < 72 && lightContrast > resultContrast) {
        result = light;
    }
    return result;
}

static COLORREF readableHeaderValueColor(COLORREF panelBg, COLORREF textPrimary, COLORREF highlightBg, COLORREF highlightText) {
    COLORREF preferred = colorLuma(panelBg) > 165 ? highlightBg : highlightText;
    COLORREF alternate = preferred == highlightBg ? highlightText : highlightBg;
    if (colorContrast(panelBg, preferred) < 70 && colorContrast(panelBg, alternate) > colorContrast(panelBg, preferred))
        preferred = alternate;
    if (colorContrast(panelBg, preferred) < 70 && colorContrast(panelBg, textPrimary) > colorContrast(panelBg, preferred))
        preferred = textPrimary;
    return preferred;
}

static int modernCandidateTextGap(int keyStyle, int textMargin) {
    switch (keyStyle) {
    case CandidateWindow::KeyStyleLeftTag:
        return max(4, textMargin);
    case CandidateWindow::KeyStyleWordFirst:
    case CandidateWindow::KeyStyleSoftCapsule:
    case CandidateWindow::KeyStyleGlowKey:
    case CandidateWindow::KeyStyleMicroTab:
        return max(3, textMargin / 2);
    default:
        return 1;
    }
}

static int modernCandidateKeyMinWidth(int keyStyle, int textMargin) {
    switch (keyStyle) {
    case CandidateWindow::KeyStyleKeycap:
    case CandidateWindow::KeyStyleBadgeMinimal:
    case CandidateWindow::KeyStyleSoftCapsule:
        return max(17, textMargin * 3 + 5);
    case CandidateWindow::KeyStyleLeftTag:
        return max(19, textMargin * 3 + 7);
    case CandidateWindow::KeyStyleAccentDot:
    case CandidateWindow::KeyStyleGlowKey:
    case CandidateWindow::KeyStyleWordAnchor:
        return max(18, textMargin * 3 + 6);
    case CandidateWindow::KeyStyleMonospaceSlot:
        return max(16, textMargin * 3 + 4);
    case CandidateWindow::KeyStyleMicroTab:
        return max(14, textMargin * 3 + 2);
    default:
        return 0;
    }
}

static int modernCandidateKeyFontPercent(int keyStyle) {
    switch (keyStyle) {
    case CandidateWindow::KeyStyleWordFirst:
    case CandidateWindow::KeyStyleMicroTab:
        return 72;
    case CandidateWindow::KeyStyleWordAnchor:
        return 82;
    default:
        return 86;
    }
}

static HFONT createScaledFont(HFONT baseFont, int percent) {
    LOGFONTW logFont;
    if (!baseFont || ::GetObjectW(baseFont, sizeof(logFont), &logFont) != sizeof(logFont))
        return NULL;

    if (logFont.lfHeight != 0) {
        int sign = logFont.lfHeight < 0 ? -1 : 1;
        int height = logFont.lfHeight < 0 ? -logFont.lfHeight : logFont.lfHeight;
        logFont.lfHeight = sign * max(1, height * percent / 100);
    }
    return ::CreateFontIndirectW(&logFont);
}

static int modernCandidateWidthSafety(int textMargin) {
    return max(2, textMargin / 2);
}

static int modernCandidateExtraWidth(int keyStyle, int textMargin) {
    int stylePadding = keyStyle == CandidateWindow::KeyStyleLeftTag ? textMargin : 0;
    return textMargin * 2 + modernCandidateTextGap(keyStyle, textMargin) + modernCandidateWidthSafety(textMargin) + stylePadding;
}

static int candidateMessageExtraWidth(int messageStyle, int textMargin, int itemHeight) {
    switch (messageStyle) {
    case CandidateWindow::MessageStyleBar:
        return max(10, textMargin * 2 + 4);
    case CandidateWindow::MessageStyleDot:
        return max(16, textMargin * 2 + 8);
    case CandidateWindow::MessageStyleBadge:
    default:
        return max(22, itemHeight) + textMargin * 2;
    }
}

static void applyCandidateWindowRegion(HWND hwnd, bool modernStyle, int width, int height, int borderRadius) {
    if (!hwnd)
        return;

    if (!modernStyle || borderRadius <= 0 || width <= 0 || height <= 0) {
        ::SetWindowRgn(hwnd, NULL, TRUE);
        return;
    }

    int diameter = borderRadius * 2;
    HRGN region = ::CreateRoundRectRgn(0, 0, width + 1, height + 1, diameter, diameter);
    if (region && ::SetWindowRgn(hwnd, region, TRUE) == 0)
        ::DeleteObject(region);
}

CandidateWindow::CandidateWindow(TextService* service, EditSession* session):
    ImeWindow(service),
    shown_(false),
    candPerRow_(1),
    effectiveCandPerRow_(1),
    textWidth_(0),
    itemHeight_(0),
    currentSel_(0),
    hasResult_(false),
    useCursor_(true),
    selKeyWidth_(0),
    modernStyle_(false),
    panelBg_(RGB(255, 255, 255)),
    panelBorder_(RGB(218, 221, 227)),
    textPrimary_(RGB(32, 36, 42)),
    textSecondary_(RGB(107, 114, 128)),
    highlightBg_(RGB(220, 235, 255)),
    highlightBorder_(RGB(156, 199, 255)),
    highlightText_(RGB(11, 58, 117)),
    contentMargin_(8),
    textMargin_(6),
    borderRadius_(8),
    keyStyle_(KeyStyleKeycap),
    messageStyle_(MessageStyleBadge),
    stableWidth_(false),
    minStableWidth_(0),
    stableWidthPx_(0),
    wrapToMaxWidth_(false),
    maxWidth_(0) {

    if(service->isImmersive()) { // windows 8 app mode
        margin_ = 10;
        rowSpacing_ = 8;
        colSpacing_ = 12;
    }
    else { // desktop mode
        margin_ = 5;
        rowSpacing_ = 4;
        colSpacing_ = 8;
    }

    HWND parent = service->compositionWindow(session);
    create(parent, WS_POPUP|WS_CLIPCHILDREN, WS_EX_TOOLWINDOW|WS_EX_TOPMOST);
}

CandidateWindow::~CandidateWindow(void) {
}

// ITfUIElement
STDMETHODIMP CandidateWindow::GetDescription(BSTR *pbstrDescription) {
    if (!pbstrDescription)
        return E_INVALIDARG;
    *pbstrDescription = SysAllocString(L"Candidate window~");
    return S_OK;
}

// {BD7CCC94-57CD-41D3-A789-AF47890CEB29}
STDMETHODIMP CandidateWindow::GetGUID(GUID *pguid) {
    if (!pguid)
        return E_INVALIDARG;
    *pguid = { 0xbd7ccc94, 0x57cd, 0x41d3, { 0xa7, 0x89, 0xaf, 0x47, 0x89, 0xc, 0xeb, 0x29 } };
    return S_OK;
}

STDMETHODIMP CandidateWindow::Show(BOOL bShow) {
    shown_ = bShow;
    if (shown_)
        show();
    else
        hide();
    return S_OK;
}

STDMETHODIMP CandidateWindow::IsShown(BOOL *pbShow) {
    if (!pbShow)
        return E_INVALIDARG;
    *pbShow = shown_;
    return S_OK;
}

// ITfCandidateListUIElement
STDMETHODIMP CandidateWindow::GetUpdatedFlags(DWORD *pdwFlags) {
    if (!pdwFlags)
        return E_INVALIDARG;
    /// XXX update all!!!
    *pdwFlags = TF_CLUIE_DOCUMENTMGR | TF_CLUIE_COUNT | TF_CLUIE_SELECTION | TF_CLUIE_STRING | TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;
    return S_OK;
}

STDMETHODIMP CandidateWindow::GetDocumentMgr(ITfDocumentMgr **ppdim) {
    if (!textService_)
        return E_FAIL;
    return textService_->currentContext()->GetDocumentMgr(ppdim);
}

STDMETHODIMP CandidateWindow::GetCount(UINT *puCount) {
    if (!puCount)
        return E_INVALIDARG;
    *puCount = std::min<UINT>(10, items_.size());
    return S_OK;
}

STDMETHODIMP CandidateWindow::GetSelection(UINT *puIndex) {
    assert(currentSel_ >= 0);
    if (!puIndex)
        return E_INVALIDARG;
    *puIndex = static_cast<UINT>(currentSel_);
    return S_OK;
}

STDMETHODIMP CandidateWindow::GetString(UINT uIndex, BSTR *pbstr) {
    if (!pbstr)
        return E_INVALIDARG;
    if (uIndex >= items_.size())
        return E_INVALIDARG;
    *pbstr = SysAllocString(items_[uIndex].c_str());
    return S_OK;
}

STDMETHODIMP CandidateWindow::GetPageIndex(UINT *puIndex, UINT uSize, UINT *puPageCnt) {
    /// XXX Always return the same single page index.
    if (!puPageCnt)
        return E_INVALIDARG;
    *puPageCnt = 1;
    if (puIndex) {
        if (uSize < *puPageCnt) {
            return E_INVALIDARG;
        }
        puIndex[0] = 0;
    }
    return S_OK;
}

STDMETHODIMP CandidateWindow::SetPageIndex(UINT *puIndex, UINT uPageCnt) {
    /// XXX Do not let app set page indices.
    if (!puIndex)
        return E_INVALIDARG;
    return S_OK;
}

STDMETHODIMP CandidateWindow::GetCurrentPage(UINT *puPage) {
    if (!puPage)
        return E_INVALIDARG;
    *puPage = 0;
    return S_OK;
}

LRESULT CandidateWindow::wndProc(UINT msg, WPARAM wp , LPARAM lp) {
    switch (msg) {
        case WM_PAINT:
            onPaint(wp, lp);
            break;
        case WM_ERASEBKGND:
            return TRUE;
            break;
        case WM_LBUTTONDOWN:
            onLButtonDown(wp, lp);
            break;
        case WM_MOUSEMOVE:
            onMouseMove(wp, lp);
            break;
        case WM_LBUTTONUP:
            onLButtonUp(wp, lp);
            break;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        default:
            return Window::wndProc(msg, wp, lp);
    }
    return 0;
}

void CandidateWindow::onPaint(WPARAM wp, LPARAM lp) {
    // TODO: check isImmersive_, and draw the window differently
    // in Windows 8 app immersive mode to follow windows 8 UX guidelines
    PAINTSTRUCT ps;
    BeginPaint(hwnd_, &ps);
    HDC hDC = ps.hdc;
    HFONT oldFont;
    RECT rc;

    oldFont = (HFONT)SelectObject(hDC, font_);

    GetClientRect(hwnd_,&rc);

    if (modernStyle_) {
        SetTextColor(hDC, textPrimary_);
        SetBkColor(hDC, panelBg_);

        // Draw rounded modern background and border
        HBRUSH bgBrush = ::CreateSolidBrush(panelBg_);
        HPEN borderPen = ::CreatePen(PS_SOLID, 1, panelBorder_);
        HGDIOBJ oldBrush = ::SelectObject(hDC, bgBrush);
        HGDIOBJ oldPen = ::SelectObject(hDC, borderPen);

        ::RoundRect(hDC, rc.left, rc.top, rc.right, rc.bottom, borderRadius_ * 2, borderRadius_ * 2);

        ::SelectObject(hDC, oldBrush);
        ::SelectObject(hDC, oldPen);
        ::DeleteObject(bgBrush);
        ::DeleteObject(borderPen);
    } else {
        SetTextColor(hDC, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(hDC, GetSysColor(COLOR_WINDOW));

        // paint window background and border
        // draw a flat black border in Windows 8 app immersive mode
        // draw a 3d border in desktop mode
        if(isImmersive()) {
            HPEN pen = ::CreatePen(PS_SOLID, 3, RGB(0, 0, 0));
            HGDIOBJ oldPen = ::SelectObject(hDC, pen);
            ::Rectangle(hDC, rc.left, rc.top, rc.right, rc.bottom);
            ::SelectObject(hDC, oldPen);
            ::DeleteObject(pen);
        }
        else {
            // draw a 3d border in desktop mode
            ::FillSolidRect(ps.hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, GetSysColor(COLOR_WINDOW));
            ::Draw3DBorder(hDC, &rc, GetSysColor(COLOR_3DFACE), 0);
        }
    }

    // paint header row (label text left-aligned, page info right-aligned)
    int headerHeight = this->headerHeight(hDC);
    if (!header_.empty() || !pageInfo_.empty()) {
        COLORREF headerLabelColor = modernStyle_ ? textSecondary_ : RGB(0, 0, 180);
        COLORREF headerValueColor = modernStyle_ ? readableHeaderValueColor(panelBg_, textPrimary_, highlightBg_, highlightText_) : RGB(0, 0, 180);
        COLORREF oldColor = ::SetTextColor(hDC, headerLabelColor);
        if (modernStyle_) {
            ::SetBkMode(hDC, TRANSPARENT);
        }

        int rowTop = modernStyle_ ? 0 : margin_;
        int rowBottom = modernStyle_ ? headerHeight : rowTop + headerHeight;
        int pageInfoLeft = rc.right - margin_ - (modernStyle_ ? textMargin_ : 0);
        if (!pageInfo_.empty()) {
            SIZE piSize;
            ::GetTextExtentPoint32W(hDC, pageInfo_.c_str(), (int)pageInfo_.length(), &piSize);
            pageInfoLeft -= piSize.cx;
        }

        if (!header_.empty()) {
            std::wstring label = L"";
            std::wstring value = header_;
            size_t separator = header_.find(L' ');
            if (separator != std::wstring::npos && separator + 1 < header_.length()) {
                label = header_.substr(0, separator);
                value = header_.substr(separator + 1);
            }

            int textX = margin_ + (modernStyle_ ? textMargin_ : 0);
            RECT labelRect = { textX, rowTop, pageInfoLeft - textMargin_, rowBottom };
            if (!label.empty()) {
                ::SetTextColor(hDC, headerLabelColor);
                ::DrawTextW(hDC, label.c_str(), (int)label.length(), &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                SIZE labelSize;
                ::GetTextExtentPoint32W(hDC, label.c_str(), (int)label.length(), &labelSize);
                labelRect.left += labelSize.cx + textMargin_;
            }

            ::SetTextColor(hDC, headerValueColor);
            ::DrawTextW(hDC, value.c_str(), (int)value.length(), &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        if (!pageInfo_.empty()) {
            SIZE piSize;
            ::GetTextExtentPoint32W(hDC, pageInfo_.c_str(), (int)pageInfo_.length(), &piSize);
            // right-aligned, vertically centered in the header row
            int piX = pageInfoLeft;
            RECT piRect = { piX, rowTop, rc.right - margin_, rowBottom };
            ::SetTextColor(hDC, headerLabelColor);
            ::DrawTextW(hDC, pageInfo_.c_str(), (int)pageInfo_.length(), &piRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        if (modernStyle_) {
            HPEN dividerPen = ::CreatePen(PS_SOLID, 1, blendColor(panelBorder_, panelBg_, 35));
            HGDIOBJ oldPen = ::SelectObject(hDC, dividerPen);
            int dividerY = max(0, headerHeight - 1);
            ::MoveToEx(hDC, 1, dividerY, NULL);
            ::LineTo(hDC, rc.right - 1, dividerY);
            ::SelectObject(hDC, oldPen);
            ::DeleteObject(dividerPen);
        }

        if (modernStyle_) {
            ::SetBkMode(hDC, OPAQUE);
        }
        ::SetTextColor(hDC, oldColor);
    }

    if (!message_.empty()) {
        int messageTop = modernStyle_ && headerHeight > 0 ? headerHeight + textMargin_ : margin_ + headerHeight;
        RECT messageRect = {
            margin_ + (modernStyle_ ? textMargin_ : 0),
            messageTop,
            rc.right - margin_ - (modernStyle_ ? textMargin_ : 0),
            rc.bottom - margin_
        };

        int oldBkMode = ::SetBkMode(hDC, TRANSPARENT);
        COLORREF oldTextColor = ::SetTextColor(hDC, modernStyle_ ? textPrimary_ : GetSysColor(COLOR_WINDOWTEXT));
        if (modernStyle_) {
            COLORREF accent = readableHeaderValueColor(panelBg_, textPrimary_, highlightBg_, highlightText_);
            COLORREF messageText = colorContrast(panelBg_, accent) >= 62 ? accent : textPrimary_;
            COLORREF messageBg = colorLuma(panelBg_) > 165 ? blendColor(panelBg_, accent, 8) : blendColor(panelBg_, accent, 13);
            RECT rowRect = messageRect;
            rowRect.bottom = min(rowRect.bottom, rowRect.top + modernCandidateRowHeight());

            if (messageStyle_ == MessageStyleBadge) {
                HBRUSH bgBrush = ::CreateSolidBrush(messageBg);
                HPEN bgPen = ::CreatePen(PS_SOLID, 1, messageBg);
                HGDIOBJ oldBrush = ::SelectObject(hDC, bgBrush);
                HGDIOBJ oldPen = ::SelectObject(hDC, bgPen);
                ::RoundRect(hDC, rowRect.left, rowRect.top, rowRect.right, rowRect.bottom, max(4, borderRadius_), max(4, borderRadius_));
                ::SelectObject(hDC, oldBrush);
                ::SelectObject(hDC, oldPen);
                ::DeleteObject(bgBrush);
                ::DeleteObject(bgPen);

                int badgeSize = min(rowRect.bottom - rowRect.top - max(2, textMargin_ / 2), max(18, itemHeight_));
                RECT badgeRect = {
                    rowRect.left + textMargin_,
                    rowRect.top + ((rowRect.bottom - rowRect.top) - badgeSize) / 2,
                    rowRect.left + textMargin_ + badgeSize,
                    rowRect.top + ((rowRect.bottom - rowRect.top) + badgeSize) / 2
                };
                HBRUSH badgeBrush = ::CreateSolidBrush(blendColor(accent, panelBg_, 12));
                HPEN badgePen = ::CreatePen(PS_SOLID, 1, blendColor(accent, panelBg_, 5));
                oldBrush = ::SelectObject(hDC, badgeBrush);
                oldPen = ::SelectObject(hDC, badgePen);
                ::RoundRect(hDC, badgeRect.left, badgeRect.top, badgeRect.right, badgeRect.bottom, max(4, badgeSize / 2), max(4, badgeSize / 2));
                ::SelectObject(hDC, oldBrush);
                ::SelectObject(hDC, oldPen);
                ::DeleteObject(badgeBrush);
                ::DeleteObject(badgePen);

                COLORREF badgeText = colorContrast(accent, highlightText_) >= 60 ? highlightText_ : panelBg_;
                ::SetTextColor(hDC, badgeText);
                ::DrawTextW(hDC, L"!", 1, &badgeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                messageRect.left = badgeRect.right + textMargin_;
            }
            else if (messageStyle_ == MessageStyleBar) {
                HPEN barPen = ::CreatePen(PS_SOLID, max(2, textMargin_ / 2), accent);
                HGDIOBJ oldPen = ::SelectObject(hDC, barPen);
                int barX = rowRect.left + max(1, textMargin_ / 3);
                ::MoveToEx(hDC, barX, rowRect.top + max(3, textMargin_ / 2), NULL);
                ::LineTo(hDC, barX, rowRect.bottom - max(3, textMargin_ / 2));
                ::SelectObject(hDC, oldPen);
                ::DeleteObject(barPen);
                messageRect.left += max(10, textMargin_ * 2);
            }
            else {
                int dotSize = max(6, min(10, itemHeight_ / 2));
                RECT dotRect = {
                    rowRect.left + textMargin_,
                    rowRect.top + ((rowRect.bottom - rowRect.top) - dotSize) / 2,
                    rowRect.left + textMargin_ + dotSize,
                    rowRect.top + ((rowRect.bottom - rowRect.top) + dotSize) / 2
                };
                HBRUSH dotBrush = ::CreateSolidBrush(accent);
                HPEN dotPen = ::CreatePen(PS_SOLID, 1, accent);
                HGDIOBJ oldBrush = ::SelectObject(hDC, dotBrush);
                HGDIOBJ oldPen = ::SelectObject(hDC, dotPen);
                ::Ellipse(hDC, dotRect.left, dotRect.top, dotRect.right, dotRect.bottom);
                ::SelectObject(hDC, oldBrush);
                ::SelectObject(hDC, oldPen);
                ::DeleteObject(dotBrush);
                ::DeleteObject(dotPen);
                messageRect.left = dotRect.right + textMargin_;
            }
            ::SetTextColor(hDC, messageText);
        }
        ::DrawTextW(hDC, message_.c_str(), (int)message_.length(), &messageRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        ::SetBkMode(hDC, oldBkMode);
        ::SetTextColor(hDC, oldTextColor);
        SelectObject(hDC, oldFont);
        EndPaint(hwnd_, &ps);
        return;
    }

    // paint items
    int col = 0;
    int x = margin_, y = margin_ + headerHeight;
    if (modernStyle_ && headerHeight > 0)
        y = headerHeight + textMargin_;
    int columnsPerRow = max(1, effectiveCandPerRow_);
    for(int i = 0, n = items_.size(); i < n; ++i) {
        paintItem(hDC, i, x, y);
        ++col; // go to next column
        if(col >= columnsPerRow) {
            col = 0;
            x = margin_;
            y += modernStyle_ ? modernCandidateRowHeight() + rowSpacing_ : itemHeight_ + rowSpacing_;
        }
        else {
            x += colSpacing_ + selKeyWidth_ + textWidth_ + (modernStyle_ ? modernCandidateExtraWidth(keyStyle_, textMargin_) : 0);
        }
    }
    SelectObject(hDC, oldFont);
    EndPaint(hwnd_, &ps);
}

void CandidateWindow::recalculateSize() {
    if (modernStyle_) {
        margin_ = contentMargin_;
        rowSpacing_ = max(0, textMargin_ / 2);
        colSpacing_ = max(6, textMargin_ + 2);
    }

    HDC hDC = ::GetWindowDC(hwnd());
    int height = 0;
    int width = 0;
    selKeyWidth_ = 0;
    textWidth_ = 0;
    itemHeight_ = 0;

    HGDIOBJ oldFont = ::SelectObject(hDC, font_);
    TEXTMETRIC textMetrics;
    ::GetTextMetrics(hDC, &textMetrics);
    int fontLineHeight = textMetrics.tmHeight + textMetrics.tmExternalLeading;
    vector<wstring>::const_iterator it;
    for(int i = 0, n = items_.size(); i < n; ++i) {
        SIZE selKeySize;
        int lineHeight = 0;
        // the selection key string
        wchar_t selKey[] = L"?. ";
        selKey[0] = selKeys_[i];
        ::GetTextExtentPoint32W(hDC, selKey, modernStyle_ ? 1 : 3, &selKeySize);
        if(selKeySize.cx > selKeyWidth_)
            selKeyWidth_ = selKeySize.cx;

        // the candidate string
        SIZE candidateSize;
        wstring& item = items_.at(i);
        ::GetTextExtentPoint32W(hDC, item.c_str(), item.length(), &candidateSize);
        if(candidateSize.cx > textWidth_)
            textWidth_ = candidateSize.cx;
        int itemHeight = max(candidateSize.cy, selKeySize.cy);
        if(itemHeight > itemHeight_)
            itemHeight_ = itemHeight;
    }
    if (itemHeight_ < fontLineHeight)
        itemHeight_ = fontLineHeight;

    // measure header (reuse the same DC)
    int headerHeight = this->headerHeight(hDC);
    int headerWidth = 0;
    int pageInfoWidth = 0;
    if (!pageInfo_.empty()) {
        SIZE pageInfoSize;
        ::GetTextExtentPoint32W(hDC, pageInfo_.c_str(), (int)pageInfo_.length(), &pageInfoSize);
        pageInfoWidth = pageInfoSize.cx;
    }

    if (!header_.empty()) {
        SIZE headerSize;
        ::GetTextExtentPoint32W(hDC, header_.c_str(), (int)header_.length(), &headerSize);
        // header row must fit both the label text and the page-info text
        headerWidth = headerSize.cx + (pageInfoWidth > 0 ? colSpacing_ * 2 + pageInfoWidth : 0);
    }
    else if (pageInfoWidth > 0) {
        // page info with no header: size the row to fit the page info alone
        headerWidth = pageInfoWidth;
    }

    int messageWidth = 0;
    int messageHeight = 0;
    if (!message_.empty()) {
        SIZE messageSize;
        ::GetTextExtentPoint32W(hDC, message_.c_str(), (int)message_.length(), &messageSize);
        messageWidth = messageSize.cx;
        messageHeight = max(messageSize.cy, fontLineHeight);
    }

    ::SelectObject(hDC, oldFont);
    ::ReleaseDC(hwnd(), hDC);

    if (modernStyle_)
        selKeyWidth_ = max(selKeyWidth_, modernCandidateKeyMinWidth(keyStyle_, textMargin_));

    int extraItemPadding = modernStyle_ ? modernCandidateExtraWidth(keyStyle_, textMargin_) : 0;
    int modernRowHeight = modernCandidateRowHeight();
    int headerGap = modernStyle_ && headerHeight > 0 ? textMargin_ : 0;
    int topPadding = modernStyle_ ? headerHeight + headerGap : margin_ + headerHeight;
    int bottomPadding = margin_;

    int itemStride = selKeyWidth_ + textWidth_ + extraItemPadding;
    int effectiveCandPerRow = max(1, candPerRow_);
    if (modernStyle_ && wrapToMaxWidth_ && maxWidth_ > 0 && itemStride > 0 && !items_.empty()) {
        int contentLimit = max(1, maxWidth_ - margin_ * 2);
        int maxColumns = (contentLimit + colSpacing_) / (itemStride + colSpacing_);
        effectiveCandPerRow = max(1, min(effectiveCandPerRow, maxColumns));
    }
    effectiveCandPerRow_ = effectiveCandPerRow;

    if (!message_.empty()) {
        int messageRowHeight = modernStyle_ ? messageHeight + textMargin_ * 2 : messageHeight;
        width = messageWidth + margin_ * 2 + (modernStyle_ ? textMargin_ * 2 : 0);
        if (modernStyle_)
            width += candidateMessageExtraWidth(messageStyle_, textMargin_, itemHeight_);
        width = max(width, headerWidth + margin_ * 2);
        height = topPadding + messageRowHeight + bottomPadding;
    }
    else if(items_.empty()) {
        width = headerWidth > 0 ? headerWidth + margin_ * 2 : margin_ * 2;
        if (modernStyle_ && headerHeight > 0)
            height = topPadding + modernRowHeight + bottomPadding;
        else
            height = headerHeight > 0 ? headerHeight + bottomPadding : margin_ * 2;
    }
    else {
        int columnCount = min((int)items_.size(), effectiveCandPerRow_);
        width = columnCount * itemStride;
        width += colSpacing_ * (columnCount - 1);
        width += margin_ * 2;
        width = max(width, headerWidth + margin_ * 2);
        int rowCount = (int)items_.size() / effectiveCandPerRow_;
        if(items_.size() % effectiveCandPerRow_)
            ++rowCount;
        height = topPadding + (modernStyle_ ? modernRowHeight : itemHeight_) * rowCount + rowSpacing_ * (rowCount - 1) + bottomPadding;
    }
    if (modernStyle_ && wrapToMaxWidth_ && maxWidth_ > 0) {
        int maxWindowWidth = max(maxWidth_, headerWidth + margin_ * 2);
        maxWindowWidth = max(maxWindowWidth, minStableWidth_);
        width = min(width, maxWindowWidth);
    }
    if (modernStyle_ && stableWidth_) {
        int minWidth = max(0, minStableWidth_);
        if (stableWidthPx_ < minWidth)
            stableWidthPx_ = minWidth;
        if (width > stableWidthPx_)
            stableWidthPx_ = width;
        if (wrapToMaxWidth_ && maxWidth_ > 0) {
            int maxWindowWidth = max(maxWidth_, headerWidth + margin_ * 2);
            maxWindowWidth = max(maxWindowWidth, minWidth);
            stableWidthPx_ = min(stableWidthPx_, maxWindowWidth);
        }
        width = stableWidthPx_;
    }
    resize(width, height);
    applyCandidateWindowRegion(hwnd_, modernStyle_, width, height, borderRadius_);
}

void CandidateWindow::setCandPerRow(int n) {
    if(n != candPerRow_) {
        candPerRow_ = n;
        recalculateSize();
    }
}

bool CandidateWindow::filterKeyEvent(KeyEvent& keyEvent) {
    // select item with arrow keys
    int oldSel = currentSel_;
    int columnsPerRow = max(1, effectiveCandPerRow_);
    int itemCount = (int)items_.size();
    switch(keyEvent.keyCode()) {
    case VK_UP:
        if(currentSel_ - columnsPerRow >=0)
            currentSel_ -= columnsPerRow;
        break;
    case VK_DOWN:
        if(currentSel_ + columnsPerRow < itemCount)
            currentSel_ += columnsPerRow;
        break;
    case VK_LEFT:
        if(currentSel_ - 1 >=0)
            --currentSel_;
        break;
    case VK_RIGHT:
        if(currentSel_ + 1 < itemCount)
            ++currentSel_;
        break;
    case VK_RETURN:
        hasResult_ = true;
        return true;
    default:
        return false;
    }
    // if currently selected item is changed, redraw
    if(currentSel_ != oldSel) {
        // repaint the old and new items
        RECT rect;
        itemRect(oldSel, rect);
        ::InvalidateRect(hwnd_, &rect, TRUE);
        itemRect(currentSel_, rect);
        ::InvalidateRect(hwnd_, &rect, TRUE);
        return true;
    }
    return false;
}

void CandidateWindow::setCurrentSel(int sel) {
    if(sel >= (int)items_.size())
        sel = 0;
    if (currentSel_ != sel) {
        currentSel_ = sel;
        if (isVisible())
            ::InvalidateRect(hwnd_, NULL, TRUE);
    }
}

void CandidateWindow::clear() {
    items_.clear();
    selKeys_.clear();
    message_.clear();
    currentSel_ = 0;
    hasResult_ = false;
}

void CandidateWindow::setUseCursor(bool use) {
    if (useCursor_ == use)
        return;
    useCursor_ = use;
    if(isVisible())
        ::InvalidateRect(hwnd_, NULL, TRUE);
}

void CandidateWindow::setStableWidth(bool stable, int minWidth) {
    minWidth = max(0, minWidth);
    if (stableWidth_ == stable && minStableWidth_ == minWidth)
        return;
    if (stableWidth_ != stable || minStableWidth_ != minWidth)
        stableWidthPx_ = 0;
    stableWidth_ = stable;
    minStableWidth_ = minWidth;
    recalculateSize();
    refresh();
}

void CandidateWindow::resetStableWidth() {
    stableWidthPx_ = 0;
}

void CandidateWindow::setMaxWidth(bool wrapToMaxWidth, int maxWidth) {
    maxWidth = max(0, maxWidth);
    if (wrapToMaxWidth_ == wrapToMaxWidth && maxWidth_ == maxWidth)
        return;
    if (wrapToMaxWidth_ != wrapToMaxWidth || maxWidth_ != maxWidth)
        stableWidthPx_ = 0;
    wrapToMaxWidth_ = wrapToMaxWidth;
    maxWidth_ = maxWidth;
    recalculateSize();
    refresh();
}

int CandidateWindow::headerHeight(HDC hDC) const {
    if (header_.empty() && pageInfo_.empty())
        return 0;

    SIZE headerSize = { 0, 0 };
    SIZE pageInfoSize = { 0, 0 };
    if (!header_.empty())
        ::GetTextExtentPoint32W(hDC, header_.c_str(), (int)header_.length(), &headerSize);
    if (!pageInfo_.empty())
        ::GetTextExtentPoint32W(hDC, pageInfo_.c_str(), (int)pageInfo_.length(), &pageInfoSize);

    int textHeight = max(headerSize.cy, pageInfoSize.cy);
    if (modernStyle_) {
        TEXTMETRIC textMetrics;
        ::GetTextMetrics(hDC, &textMetrics);
        textHeight = max(textHeight, textMetrics.tmHeight + textMetrics.tmExternalLeading);
        return textHeight + textMargin_ * 2;
    }
    return textHeight + margin_;
}

int CandidateWindow::modernCandidateRowHeight() const {
    return itemHeight_ + textMargin_ * 2;
}

void CandidateWindow::paintItem(HDC hDC, int i,  int x, int y) {
    if (modernStyle_) {
        RECT itemRc;
        itemRect(i, itemRc);
        ::InflateRect(&itemRc, 0, -max(1, textMargin_ / 3));

        bool isSelected = (useCursor_ && i == currentSel_);
        COLORREF selectedBg = blendColor(panelBg_, highlightBg_, 28);
        COLORREF selectedFg = readableTextOnColor(selectedBg, highlightText_, textPrimary_);
        COLORREF selectedMutedFg = blendColor(selectedFg, selectedBg, 18);
        COLORREF selectedBadgeBg = blendColor(selectedBg, highlightBg_, colorLuma(panelBg_) > 165 ? 42 : 30);
        COLORREF selectedBadgeFg = readableTextOnColor(selectedBadgeBg, highlightText_, textPrimary_);
        COLORREF selectedBorder = colorContrast(selectedBg, highlightBorder_) >= 38
            ? blendColor(highlightBorder_, selectedBg, 28)
            : blendColor(selectedFg, selectedBg, 40);

        // Fill background of the item
        if (isSelected) {
            HBRUSH bgBrush = ::CreateSolidBrush(selectedBg);
            HPEN borderPen = ::CreatePen(PS_SOLID, 1, selectedBorder);
            HGDIOBJ oldBrush = ::SelectObject(hDC, bgBrush);
            HGDIOBJ oldPen = ::SelectObject(hDC, borderPen);

            ::RoundRect(hDC, itemRc.left, itemRc.top, itemRc.right, itemRc.bottom, borderRadius_ * 2, borderRadius_ * 2);

            ::SelectObject(hDC, oldBrush);
            ::SelectObject(hDC, oldPen);
            ::DeleteObject(bgBrush);
            ::DeleteObject(borderPen);
        }

        if (keyStyle_ == KeyStyleLeftTag) {
            HPEN itemPen = ::CreatePen(PS_SOLID, 1, isSelected ? blendColor(selectedFg, selectedBg, 22) : blendColor(textSecondary_, panelBg_, 82));
            HGDIOBJ oldPen = ::SelectObject(hDC, itemPen);
            HGDIOBJ oldBrush = ::SelectObject(hDC, ::GetStockObject(HOLLOW_BRUSH));
            ::RoundRect(hDC, itemRc.left, itemRc.top, itemRc.right, itemRc.bottom, max(4, borderRadius_), max(4, borderRadius_));
            ::SelectObject(hDC, oldBrush);
            ::SelectObject(hDC, oldPen);
            ::DeleteObject(itemPen);
        }

        if (keyStyle_ == KeyStyleRail) {
            HPEN railPen = ::CreatePen(PS_SOLID, 2, isSelected ? blendColor(selectedFg, selectedBg, 15) : blendColor(textSecondary_, panelBg_, 70));
            HGDIOBJ oldPen = ::SelectObject(hDC, railPen);
            int railInset = max(4, textMargin_);
            int railX = itemRc.left + max(2, textMargin_ / 2);
            ::MoveToEx(hDC, railX, itemRc.top + railInset, NULL);
            ::LineTo(hDC, railX, itemRc.bottom - railInset);
            ::SelectObject(hDC, oldPen);
            ::DeleteObject(railPen);
        }

        // Draw selection key
        wchar_t selKey[] = L"?. ";
        selKey[0] = selKeys_[i];
        int textGap = modernCandidateTextGap(keyStyle_, textMargin_);
        bool wordFirst = keyStyle_ == KeyStyleWordFirst;
        RECT keyRc = { itemRc.left + textMargin_, itemRc.top, itemRc.left + textMargin_ + selKeyWidth_, itemRc.bottom };
        RECT textRc = { keyRc.right + textGap, itemRc.top, itemRc.right - textMargin_, itemRc.bottom };
        if (wordFirst) {
            textRc.left = itemRc.left + textMargin_;
            textRc.right = min(itemRc.right - textMargin_ - selKeyWidth_ - textGap, textRc.left + textWidth_);
            keyRc.left = textRc.right + textGap;
            keyRc.right = min(itemRc.right - textMargin_, keyRc.left + selKeyWidth_);
        }

        COLORREF keyColor = isSelected ? selectedFg : textSecondary_;
        if (keyStyle_ == KeyStyleQuiet || keyStyle_ == KeyStyleWordAnchor)
            keyColor = isSelected ? selectedMutedFg : blendColor(textSecondary_, panelBg_, 45);
        else if (keyStyle_ == KeyStyleGlowKey)
            keyColor = isSelected ? selectedFg : blendColor(textSecondary_, panelBg_, 30);
        else if (keyStyle_ == KeyStyleWordFirst)
            keyColor = isSelected ? selectedMutedFg : blendColor(textSecondary_, panelBg_, 38);

        int oldBkMode = ::SetBkMode(hDC, TRANSPARENT);
        COLORREF oldTextColor = ::SetTextColor(hDC, keyColor);
        HFONT keyFont = createScaledFont(font_, modernCandidateKeyFontPercent(keyStyle_));
        HGDIOBJ oldKeyFont = keyFont ? ::SelectObject(hDC, keyFont) : NULL;

        if (keyStyle_ == KeyStyleKeycap) {
            if (isSelected) {
                RECT badgeRc = keyRc;
                ::InflateRect(&badgeRc, 0, -max(1, textMargin_ / 2));
                COLORREF badgeBg = selectedBadgeBg;
                COLORREF badgeBorder = blendColor(selectedBorder, badgeBg, 28);
                HBRUSH badgeBrush = ::CreateSolidBrush(badgeBg);
                HPEN badgePen = ::CreatePen(PS_SOLID, 1, badgeBorder);
                HGDIOBJ oldBrush = ::SelectObject(hDC, badgeBrush);
                HGDIOBJ oldPen = ::SelectObject(hDC, badgePen);
                int badgeRadius = max(3, min(borderRadius_, max(3, (badgeRc.bottom - badgeRc.top) / 2)));
                ::RoundRect(hDC, badgeRc.left, badgeRc.top, badgeRc.right + 1, badgeRc.bottom, badgeRadius, badgeRadius);
                ::SelectObject(hDC, oldBrush);
                ::SelectObject(hDC, oldPen);
                ::DeleteObject(badgeBrush);
                ::DeleteObject(badgePen);
                ::SetTextColor(hDC, selectedBadgeFg);
            }
            else {
                ::SetTextColor(hDC, keyColor);
            }
            ::DrawTextW(hDC, selKey, 1, &keyRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else if (keyStyle_ == KeyStyleBadgeMinimal || keyStyle_ == KeyStyleSoftCapsule || keyStyle_ == KeyStyleLeftTag) {
            RECT badgeRc = keyRc;
            if (keyStyle_ == KeyStyleLeftTag) {
                badgeRc.left = itemRc.left + max(1, textMargin_ / 2);
                badgeRc.right = badgeRc.left + selKeyWidth_;
                keyRc = badgeRc;
            }
            ::InflateRect(&badgeRc, 0, -max(1, textMargin_ / 2));

            COLORREF badgeBg = keyStyle_ == KeyStyleSoftCapsule
                ? (isSelected ? blendColor(selectedBg, highlightBg_, 24) : blendColor(panelBg_, textSecondary_, 10))
                : (isSelected ? selectedBadgeBg : panelBg_);
            COLORREF badgeBorder = keyStyle_ == KeyStyleSoftCapsule
                ? (isSelected ? blendColor(selectedBorder, badgeBg, 38) : badgeBg)
                : (isSelected ? blendColor(selectedBorder, badgeBg, 28) : blendColor(textSecondary_, panelBg_, 62));

            HBRUSH badgeBrush = ::CreateSolidBrush(badgeBg);
            HPEN badgePen = ::CreatePen(PS_SOLID, 1, badgeBorder);
            HGDIOBJ oldBrush = ::SelectObject(hDC, badgeBrush);
            HGDIOBJ oldPen = ::SelectObject(hDC, badgePen);
            int badgeRadius = keyStyle_ == KeyStyleSoftCapsule ? (badgeRc.bottom - badgeRc.top) : max(3, min(borderRadius_, max(3, (badgeRc.bottom - badgeRc.top) / 2)));
            ::RoundRect(hDC, badgeRc.left, badgeRc.top, badgeRc.right + 1, badgeRc.bottom, badgeRadius, badgeRadius);
            ::SelectObject(hDC, oldBrush);
            ::SelectObject(hDC, oldPen);
            ::DeleteObject(badgeBrush);
            ::DeleteObject(badgePen);
            ::SetTextColor(hDC, isSelected ? readableTextOnColor(badgeBg, selectedBadgeFg, selectedFg) : keyColor);
            ::DrawTextW(hDC, selKey, 1, &keyRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else if (keyStyle_ == KeyStyleAccentDot) {
            RECT markerRc = {
                keyRc.left + 1,
                keyRc.top + (keyRc.bottom - keyRc.top) / 2 - (isSelected ? 5 : 2),
                keyRc.left + (isSelected ? 5 : 4),
                keyRc.top + (keyRc.bottom - keyRc.top) / 2 + (isSelected ? 6 : 2)
            };
            HBRUSH markerBrush = ::CreateSolidBrush(isSelected ? selectedFg : blendColor(highlightBorder_, panelBg_, 20));
            HPEN markerPen = ::CreatePen(PS_SOLID, 1, isSelected ? selectedFg : blendColor(highlightBorder_, panelBg_, 20));
            HGDIOBJ oldBrush = ::SelectObject(hDC, markerBrush);
            HGDIOBJ oldPen = ::SelectObject(hDC, markerPen);
            ::RoundRect(hDC, markerRc.left, markerRc.top, markerRc.right, markerRc.bottom, 4, 4);
            ::SelectObject(hDC, oldBrush);
            ::SelectObject(hDC, oldPen);
            ::DeleteObject(markerBrush);
            ::DeleteObject(markerPen);
            RECT keyTextRc = keyRc;
            keyTextRc.left += 6;
            ::DrawTextW(hDC, selKey, 1, &keyTextRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else if (keyStyle_ == KeyStyleMicroTab) {
            RECT tabRc = keyRc;
            tabRc.top += max(1, textMargin_ / 2);
            tabRc.bottom = min(tabRc.bottom, tabRc.top + max(11, itemHeight_ / 2));
            tabRc.left += 1;
            tabRc.right -= 1;
            COLORREF tabBg = isSelected ? selectedBadgeBg : blendColor(panelBg_, textSecondary_, 11);
            HBRUSH tabBrush = ::CreateSolidBrush(tabBg);
            HPEN tabPen = ::CreatePen(PS_SOLID, 1, isSelected ? blendColor(selectedBorder, tabBg, 28) : tabBg);
            HGDIOBJ oldBrush = ::SelectObject(hDC, tabBrush);
            HGDIOBJ oldPen = ::SelectObject(hDC, tabPen);
            ::RoundRect(hDC, tabRc.left, tabRc.top, tabRc.right + 1, tabRc.bottom, 4, 4);
            ::SelectObject(hDC, oldBrush);
            ::SelectObject(hDC, oldPen);
            ::DeleteObject(tabBrush);
            ::DeleteObject(tabPen);
            ::SetTextColor(hDC, isSelected ? readableTextOnColor(tabBg, selectedBadgeFg, selectedFg) : keyColor);
            ::DrawTextW(hDC, selKey, 1, &tabRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else {
            UINT keyFormat = keyStyle_ == KeyStyleDivider ? DT_CENTER : DT_LEFT;
            if (keyStyle_ == KeyStyleMonospaceSlot)
                keyFormat = DT_CENTER;
            UINT keyVerticalFormat = keyStyle_ == KeyStyleWordFirst ? DT_TOP : DT_VCENTER;
            if (keyStyle_ == KeyStyleGlowKey && isSelected) {
                COLORREF glowColor = blendColor(selectedFg, selectedBg, 30);
                ::SetTextColor(hDC, glowColor);
                RECT glowRc = keyRc;
                ::OffsetRect(&glowRc, 1, 0);
                ::DrawTextW(hDC, selKey, 1, &glowRc, keyFormat | keyVerticalFormat | DT_SINGLELINE);
                glowRc = keyRc;
                ::OffsetRect(&glowRc, -1, 0);
                ::DrawTextW(hDC, selKey, 1, &glowRc, keyFormat | keyVerticalFormat | DT_SINGLELINE);
                ::SetTextColor(hDC, keyColor);
            }
            if (keyStyle_ == KeyStyleWordFirst)
                keyRc.top += max(2, textMargin_ / 2);
            ::DrawTextW(hDC, selKey, 1, &keyRc, keyFormat | keyVerticalFormat | DT_SINGLELINE);
            if (keyStyle_ == KeyStyleDivider) {
                HPEN dividerPen = ::CreatePen(PS_SOLID, 1, isSelected ? blendColor(selectedFg, selectedBg, 26) : blendColor(panelBorder_, textSecondary_, 35));
                HGDIOBJ oldPen = ::SelectObject(hDC, dividerPen);
                int dividerInset = max(3, textMargin_ / 2);
                int dividerX = keyRc.right - max(3, textMargin_);
                dividerX = max(keyRc.left + 1, min(dividerX, keyRc.right - 1));
                ::MoveToEx(hDC, dividerX, keyRc.top + dividerInset, NULL);
                ::LineTo(hDC, dividerX, keyRc.bottom - dividerInset);
                ::SelectObject(hDC, oldPen);
                ::DeleteObject(dividerPen);
            }
        }

        if (keyFont) {
            ::SelectObject(hDC, oldKeyFont);
            ::DeleteObject(keyFont);
        }

        // Draw candidate text
        wstring& item = items_.at(i);
        COLORREF textColor = isSelected ? selectedFg : textPrimary_;
        ::SetTextColor(hDC, textColor);
        ::DrawTextW(hDC, item.c_str(), (int)item.length(), &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (keyStyle_ == KeyStyleWordAnchor) {
            SIZE itemSize;
            ::GetTextExtentPoint32W(hDC, item.c_str(), (int)item.length(), &itemSize);
            HPEN anchorPen = ::CreatePen(PS_SOLID, 1, isSelected ? blendColor(selectedFg, selectedBg, 20) : blendColor(textSecondary_, panelBg_, 45));
            HGDIOBJ oldPen = ::SelectObject(hDC, anchorPen);
            int anchorY = textRc.bottom - max(3, textMargin_ / 2);
            ::MoveToEx(hDC, textRc.left, anchorY, NULL);
            ::LineTo(hDC, min(textRc.left + itemSize.cx, textRc.right), anchorY);
            ::SelectObject(hDC, oldPen);
            ::DeleteObject(anchorPen);
        }

        ::SetTextColor(hDC, oldTextColor);
        ::SetBkMode(hDC, oldBkMode);
    } else {
        RECT textRect = {x, y, 0, y + itemHeight_};
        wchar_t selKey[] = L"?. ";
        selKey[0] = selKeys_[i];
        textRect.right = textRect.left + selKeyWidth_;
        // FIXME: make the color of strings configurable.
        COLORREF selKeyColor = RGB(0, 0, 255);
        COLORREF oldColor = ::SetTextColor(hDC, selKeyColor);
        // paint the selection key
        ::ExtTextOut(hDC, textRect.left, textRect.top, ETO_OPAQUE, &textRect, selKey, 3, NULL);
        ::SetTextColor(hDC, oldColor); // restore text color

        // paint the candidate string
        wstring& item = items_.at(i);
        textRect.left += selKeyWidth_;
        textRect.right = textRect.left + textWidth_;
        // paint the candidate string
        ::ExtTextOut(hDC, textRect.left, textRect.top, ETO_OPAQUE, &textRect, item.c_str(), item.length(), NULL);

        if(useCursor_ && i == currentSel_) { // invert the selected item
            int left = textRect.left; // - selKeyWidth_;
            int top = textRect.top;
            int width = textRect.right - left;
            int height = itemHeight_;
            ::BitBlt(hDC, left, top, width, itemHeight_, hDC, left, top, NOTSRCCOPY);
        }
    }
}

void CandidateWindow::itemRect(int i, RECT& rect) {
    int row, col;
    int columnsPerRow = max(1, effectiveCandPerRow_);
    row = i / columnsPerRow;
    col = i % columnsPerRow;
    if (modernStyle_) {
        int extraItemPadding = modernCandidateExtraWidth(keyStyle_, textMargin_);
        rect.left = margin_ + col * (selKeyWidth_ + textWidth_ + colSpacing_ + extraItemPadding);

        // measure header height
        int headerHeight = 0;
        if (!header_.empty() || !pageInfo_.empty()) {
            HDC hDC = ::GetWindowDC(hwnd());
            HGDIOBJ oldFont = ::SelectObject(hDC, font_);
            headerHeight = this->headerHeight(hDC);
            ::SelectObject(hDC, oldFont);
            ::ReleaseDC(hwnd(), hDC);
        }

        int headerGap = headerHeight > 0 ? textMargin_ : 0;
        int rowHeight = modernCandidateRowHeight();
        rect.top = headerHeight + headerGap + row * (rowHeight + rowSpacing_);
        rect.right = rect.left + (selKeyWidth_ + textWidth_ + extraItemPadding);
        rect.bottom = rect.top + rowHeight;
    } else {
        rect.left = margin_ + col * (selKeyWidth_ + textWidth_ + colSpacing_);

        // measure header height
        int headerHeight = 0;
        if (!header_.empty()) {
            HDC hDC = ::GetWindowDC(hwnd());
            HGDIOBJ oldFont = ::SelectObject(hDC, font_);
            SIZE headerSize;
            ::GetTextExtentPoint32W(hDC, header_.c_str(), (int)header_.length(), &headerSize);
            ::SelectObject(hDC, oldFont);
            ::ReleaseDC(hwnd(), hDC);
            headerHeight = headerSize.cy + margin_;
        }

        rect.top = margin_ + headerHeight + row * (itemHeight_ + rowSpacing_);
        rect.right = rect.left + (selKeyWidth_ + textWidth_);
        rect.bottom = rect.top + itemHeight_;
    }
}


} // namespace Ime
