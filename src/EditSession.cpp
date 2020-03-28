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

#include "EditSession.h"
#include "TextService.h"
#include <assert.h>

namespace Ime {

EditSession::EditSession(ComPtr<ITfContext> context, std::function<void(EditSession*, TfEditCookie)>&& callback):
    context_{std::move(context)},
    editCookie_{0},
    callback_{std::move(callback)} {
}

EditSession::~EditSession(void) {
}

// COM stuff

STDMETHODIMP EditSession::DoEditSession(TfEditCookie ec) {
    editCookie_ = ec;
    callback_(this, ec);
    editCookie_ = 0;
    return S_OK;
}

} // namespace Ime
