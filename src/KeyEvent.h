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

#pragma once

#include <Windows.h>

namespace Ime {

class KeyEvent {
public:
    KeyEvent() = delete;
    KeyEvent(const KeyEvent& other) = default;
    KeyEvent(UINT type, WPARAM wp, LPARAM lp);
    ~KeyEvent(void);

    UINT type() const {
        return type_;
    }

    UINT keyCode() const {
        return keyCode_;
    }

    UINT charCode() const {
        return charCode_;
    }

    bool isChar() const {
        return (charCode_ != 0);
    }

    LPARAM lParam() const {
        return lParam_;
    }

    unsigned short repeatCount() const {
        // bits 0-15
        return (unsigned short)(lParam_ & 0xffff);
    }

    unsigned char scanCode() const {
        // bits 16-23
        return (unsigned char)(lParam_ & 0xff0000);
    }

    bool isExtended() const {
        // bit 24
        return (lParam_ & (1<<24)) != 0;
    }

    bool isKeyDown(UINT code) const {
        return (keyStates_[code] & (1 << 7)) != 0;
    }

    bool isKeyToggled(UINT code) const {
        return (keyStates_[code] & 1) != 0;
    }

    const BYTE* keyStates() const {
        return keyStates_;
    }

private:
    UINT type_;
    UINT keyCode_;
    UINT charCode_;
    LPARAM lParam_;
    BYTE keyStates_[256];
};

// Try to use KeyEvent::isKeyDown() and KeyEvent::isKeyToggled() whenever possible.
// If a KeyEvent object is not available, then build a KeyState object as an alternative
// to get key states
class KeyState {
public:
    KeyState(int keyCode, bool sync = true) {
        state_ = ::GetKeyState(keyCode);
    }

    bool isDown() const {
        return ((state_ & (1 << 15)) != 0);
    }

    bool isToggled() const {
        return ((state_ & 1) != 0);
    }

    short state() const {
        return state_;
    }

private:
    short state_;
};

}
