/**
 * Copyright © 2007-2011 Kirill Gavrilov <kirill@sview.ru>
 *
 * StCore library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * StCore library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#if(defined(__linux__) || defined(__linux))

#include "StXDisplay.h"
#include <StStrings/StLogger.h>

StXDisplay::StXDisplay()
: hDisplay(NULL),
  hVisInfo(NULL),
  wndProtocols(None),
  wndDestroyAtom(None),
  xDNDEnter(None),
  xDNDPosition(None),
  xDNDStatus(None),
  xDNDTypeList(None),
  xDNDActionCopy(None),
  xDNDDrop(None),
  xDNDLeave(None),
  xDNDFinished(None),
  xDNDSelection(None),
  xDNDProxy(None),
  xDNDAware(None),
  xDNDPlainText(None),
  XA_TARGETS(None) {
    open();
}

StXDisplay::~StXDisplay() {
    close();
}

bool StXDisplay::open() {
    hDisplay = XOpenDisplay(NULL); // get first display on server from DISPLAY in env
    //hDisplay = XOpenDisplay(":0.0");
    //hDisplay = XOpenDisplay("somehost:0.0");
    //hDisplay = XOpenDisplay("192.168.1.10:0.0");
    if(isOpened()) {
        initAtoms();
        return true;
    }
    return false;
}

void StXDisplay::close() {
    if(hDisplay != NULL) {
        XCloseDisplay(hDisplay);
        hDisplay = NULL;
    }
    if(hVisInfo != NULL) {
        XFree(hVisInfo);
        hVisInfo = NULL;
    }
}

void StXDisplay::initAtoms() {
    wndDestroyAtom = XInternAtom(hDisplay, "WM_DELETE_WINDOW", True);
    wndProtocols   = XInternAtom(hDisplay, "WM_PROTOCOLS",     True);

    // Atoms for Xdnd
    xDNDEnter      = XInternAtom(hDisplay, "XdndEnter",        False);
    xDNDPosition   = XInternAtom(hDisplay, "XdndPosition",     False);
    xDNDStatus     = XInternAtom(hDisplay, "XdndStatus",       False);
    xDNDTypeList   = XInternAtom(hDisplay, "XdndTypeList",     False);
    xDNDActionCopy = XInternAtom(hDisplay, "XdndActionCopy",   False);
    xDNDDrop       = XInternAtom(hDisplay, "XdndDrop",         False);
    xDNDLeave      = XInternAtom(hDisplay, "XdndLeave",        False);
    xDNDFinished   = XInternAtom(hDisplay, "XdndFinished",     False);
    xDNDSelection  = XInternAtom(hDisplay, "XdndSelection",    False);
    xDNDProxy      = XInternAtom(hDisplay, "XdndProxy",        False);
    xDNDAware      = XInternAtom(hDisplay, "XdndAware",        False);
    xDNDPlainText  = XInternAtom(hDisplay, "text/plain",       False); //"UTF8_STRING", "COMPOUND_TEXT"
    // This is a meta-format for data to be "pasted" in to.
    // Requesting this format acquires a list of possible
    // formats from the application which copied the data.
    XA_TARGETS     = XInternAtom(hDisplay, "TARGETS",          False);

}

Property StXDisplay::readProperty(Window hWindow, Atom property) const {
    Atom actualType;
    int actualFormat = 0;
    unsigned long nItems = 0;
    unsigned long bytesAfter = 0;
    unsigned char* ret = NULL;
    int readBytes = 1024;

    // Keep trying to read the property until there are no
    // bytes unread
    do {
        if(ret != NULL) {
            XFree(ret);
        }
        XGetWindowProperty(hDisplay, hWindow, property, 0, readBytes, False, AnyPropertyType,
                           &actualType, &actualFormat, &nItems, &bytesAfter,
                           &ret);
        readBytes *= 2;
    } while(bytesAfter != 0);
    /*ST_DEBUG_LOG("Actual type: " + getAtomName(actualType) + "\n"
        + "Actual format: " + actualFormat + "\n"
        + "Number of items: " + nItems + "\n"
    );*/
    Property aProperty = {ret, actualFormat, int(nItems), actualType};
    /// TODO (Kirill Gavrilov#9) - possible memory leek (no XFree(ret)) - should be avoided using Handles?
    return aProperty;
}

#endif
