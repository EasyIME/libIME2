//
//	Copyright (C) 2013 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
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

#ifndef IME_EDIT_SESSION_H
#define IME_EDIT_SESSION_H

#include <msctf.h>
#include "ComObject.h"

namespace Ime {

class TextService;

class EditSession: public ComObject<ComInterface<ITfEditSession>> {
public:
	EditSession(TextService* service, ITfContext* context);

	TextService* textService() {
		return textService_;
	}

	ITfContext* context() {
		return context_;
	}

	TfEditCookie editCookie() {
		return editCookie_;
	}

	// COM stuff

    // ITfEditSession
    virtual STDMETHODIMP DoEditSession(TfEditCookie ec);

protected: // COM object should not be deleted directly. calling Release() instead.
	virtual ~EditSession(void);

protected:
	TextService* textService_;
	ITfContext* context_;

private:
	TfEditCookie editCookie_;
};

}

#endif
