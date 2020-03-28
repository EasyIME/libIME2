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

#include "DisplayAttributeProvider.h"
#include "DisplayAttributeInfo.h"
#include "DisplayAttributeInfoEnum.h"
#include "ImeModule.h"
#include <assert.h>

using namespace std;

namespace Ime {

DisplayAttributeProvider::DisplayAttributeProvider(ImeModule* module):
    imeModule_(module) {
}

DisplayAttributeProvider::~DisplayAttributeProvider(void) {
}

// COM stuff

// ITfDisplayAttributeProvider
STDMETHODIMP DisplayAttributeProvider::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo **ppEnum) {
    *ppEnum = (IEnumTfDisplayAttributeInfo*)new DisplayAttributeInfoEnum(this);
    return S_OK;
}

STDMETHODIMP DisplayAttributeProvider::GetDisplayAttributeInfo(REFGUID guidInfo, ITfDisplayAttributeInfo **ppInfo) {
    for(auto& info: imeModule_->displayAttrInfos()) {
        if(::IsEqualGUID(info->guid(), guidInfo)) {
            *ppInfo = info;
            info->AddRef();
            return S_OK;
        }
    }
    return E_INVALIDARG;
}

} // namespace Ime
