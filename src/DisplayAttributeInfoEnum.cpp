//
//	Copyright (C) 2013 - 2020 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
//
//	This library is free software; you can redistribute it and/or
//	modify it under the terms of the GNU Library General Public
//	License as published by the Free Software Foundation; either
//	version 2 of the License, or (at your option) any later version.
//
//	This library is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//	Library General Public License for more details.
//
//	You should have received a copy of the GNU Library General Public
//	License along with this library; if not, write to the
//	Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
//	Boston, MA  02110-1301, USA.
//

#include "DisplayAttributeInfoEnum.h"
#include "DisplayAttributeProvider.h"
#include "ImeModule.h"
#include <assert.h>

namespace Ime {

DisplayAttributeInfoEnum::DisplayAttributeInfoEnum(ComPtr<DisplayAttributeProvider> provider):
	provider_(std::move(provider)) {
	std::list<DisplayAttributeInfo*>& displayAttrInfos = provider_->imeModule_->displayAttrInfos();
	iterator_ = displayAttrInfos.begin();
}

DisplayAttributeInfoEnum::~DisplayAttributeInfoEnum(void) {
}

// IEnumTfDisplayAttributeInfo
STDMETHODIMP DisplayAttributeInfoEnum::Clone(IEnumTfDisplayAttributeInfo **ppEnum) {
	*ppEnum = static_cast<IEnumTfDisplayAttributeInfo*>(new DisplayAttributeInfoEnum(*this));
	return S_OK;
}

STDMETHODIMP DisplayAttributeInfoEnum::Next(ULONG ulCount, ITfDisplayAttributeInfo **rgInfo, ULONG *pcFetched) {
	ULONG n = 0;
    if (rgInfo != nullptr) {
        std::list<DisplayAttributeInfo*>& displayAttrInfos = provider_->imeModule_->displayAttrInfos();
        for (; n < ulCount && iterator_ != displayAttrInfos.end(); ++n, ++iterator_) {
            DisplayAttributeInfo* info = *iterator_;
            info->AddRef();
            rgInfo[n] = info;
        }
    }
    if (pcFetched) {
        *pcFetched = n;
    }
	return n < ulCount ? S_FALSE : S_OK;
}

STDMETHODIMP DisplayAttributeInfoEnum::Reset() {
	std::list<DisplayAttributeInfo*>& displayAttrInfos = provider_->imeModule_->displayAttrInfos();
	iterator_ = displayAttrInfos.begin();
	return S_OK;
}

STDMETHODIMP DisplayAttributeInfoEnum::Skip(ULONG ulCount) {
	std::list<DisplayAttributeInfo*>& displayAttrInfos = provider_->imeModule_->displayAttrInfos();
	if(iterator_ != displayAttrInfos.end())
		++iterator_;
	return S_OK;
}

} // namespace Ime

