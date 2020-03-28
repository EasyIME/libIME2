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

#include "DisplayAttributeInfo.h"
#include <assert.h>

namespace Ime {

DisplayAttributeInfo::DisplayAttributeInfo(const GUID& guid):
    atom_(0),
    guid_(guid) {

    Reset();
}

DisplayAttributeInfo::~DisplayAttributeInfo(void) {
}

// COM stuff

// ITfDisplayAttributeInfo
STDMETHODIMP DisplayAttributeInfo::GetGUID(GUID *pguid) {
    *pguid = guid_;
    return S_OK;
}

STDMETHODIMP DisplayAttributeInfo::GetDescription(BSTR *pbstrDesc) {
    *pbstrDesc = ::SysAllocString(desc_.c_str());
    return *pbstrDesc ? S_OK : E_FAIL;
}

STDMETHODIMP DisplayAttributeInfo::GetAttributeInfo(TF_DISPLAYATTRIBUTE *ptfDisplayAttr) {
    *ptfDisplayAttr = attrib_;
    return S_OK;
}

STDMETHODIMP DisplayAttributeInfo::SetAttributeInfo(const TF_DISPLAYATTRIBUTE *ptfDisplayAttr) {
    attrib_ = *ptfDisplayAttr;
    return S_OK;
}

STDMETHODIMP DisplayAttributeInfo::Reset() {
    attrib_.bAttr = TF_ATTR_INPUT;
    attrib_.crBk.type = TF_CT_NONE;
    attrib_.crLine.type = TF_CT_NONE;
    attrib_.crText.type = TF_CT_NONE;
    attrib_.fBoldLine = FALSE;
    attrib_.lsStyle = TF_LS_NONE;
    return S_OK;
}

} // namespace Ime
