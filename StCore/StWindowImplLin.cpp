/**
 * Copyright © 2007-2012 Kirill Gavrilov <kirill@sview.ru>
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

#include "StWindowImpl.h"
#include "stvkeysxarray.h" // X keys to VKEYs lookup array

#include <StStrings/StLogger.h>

#include <X11/extensions/Xrandr.h>
#include <X11/xpm.h>
#include <gdk/gdkx.h>
#include <cmath>

#include "../share/sView/icons/menu.xpm"

static int dblBuff[] = {
    GLX_RGBA,
    GLX_DEPTH_SIZE, 16,
    GLX_DOUBLEBUFFER,
    None
};

static int quadBuff[] = {
    GLX_RGBA,
    GLX_DEPTH_SIZE, 16,
    GLX_DOUBLEBUFFER,
    GLX_STEREO,
    None
};

/**
 * Our own XError handle function.
 * Default handlers just show the error and exit application immediately.
 * This is very horrible for application, because many errors not critical and could be ignored.
 * The main target for this function creation - prevent sView to quit after
 * multiple BadMatch errors happens on fullscreen->windowed switching
 * (looking like a vendors OpenGL driver bug).
 * Thus, now we just show up the error description and just ignore it - hopes we will well.
 * Function behaviour could be extended in future.
 */
int stXErrorHandler(Display*     theDisplay,
                    XErrorEvent* theErrorEvent) {
    char aBuffer[4096];
    XGetErrorText(theDisplay, theErrorEvent->error_code, aBuffer, 4096);
    ST_DEBUG_LOG("XError happend: " + aBuffer + "; ignored");
    // have no idea WHAT we should return here....
    return 0;
}

Bool StWindowImpl::stXWaitMapped(Display* theDisplay,
                                 XEvent*  theEvent,
                                 char*    theArg) {
    return (theEvent->type        == MapNotify)
        && (theEvent->xmap.window == (Window )theArg);
}

// function create GUI window
bool StWindowImpl::stglCreate(const StWinAttributes_t* theAttributes,
                              const StNativeWin_t*     theParentWindow) {
    if(theParentWindow != NULL) {
        stMemCpy(&myParentWin, theParentWindow, sizeof(StNativeWin_t));
        myParentWin.stWinPtr = this;
    }

    // initialize helper GDK
    static bool isGdkInitialized = false;
    if(!isGdkInitialized) {
        if(!gdk_init_check(NULL, NULL)) {
            stError("GDK, init failed");
            //myInitState =
            return false;
        }
        gdk_rgb_init(); // only guess sets up the true colour colour map
        isGdkInitialized = true;
    }

    size_t aBytesToCopy = (theAttributes->nSize > sizeof(StWinAttributes_t)) ? sizeof(StWinAttributes_t) : theAttributes->nSize;
    stMemCpy(&myWinAttribs, theAttributes, aBytesToCopy); // copy as much as possible
    myWinAttribs.nSize = sizeof(StWinAttributes_t);       // restore own size
    updateSlaveConfig();

    // replace default XError handler to ignore some errors
    /// TODO (Kirill Gavrilov#1) - GTK+ (re)initialization seems to be override our error handler!
    XSetErrorHandler(stXErrorHandler);

    myInitState = STWIN_INITNOTSTART;
    // X-server implementation
    // create window on unix systems throw X-server
    XSetWindowAttributes aWinAttribsX;
    int                dummy;

    // open a connection to the X server
    StXDisplayH stXDisplay = new StXDisplay();
    if(!stXDisplay->isOpened()) {
        stXDisplay.nullify();
        stError("X, could not open display");
        myInitState = STWIN_ERROR_X_OPENDISPLAY;
        return false;
    }
    myMaster.stXDisplay = stXDisplay;
    Display* hDisplay = stXDisplay->hDisplay;

    // make sure OpenGL's GLX extension supported
    if(!glXQueryExtension(hDisplay, &dummy, &dummy)) {
        myMaster.close();
        stError("X, server has no OpenGL GLX extension");
        myInitState = STWIN_ERROR_X_NOGLX;
        return false;
    }

    if(myWinAttribs.isGlStereo) {
        // find an appropriate visual
        stXDisplay->hVisInfo = glXChooseVisual(hDisplay, DefaultScreen(hDisplay), quadBuff);
        if(stXDisplay->hVisInfo == NULL) {
            stError("X, no Quad Buffered visual");
            stXDisplay->hVisInfo = glXChooseVisual(hDisplay, DefaultScreen(hDisplay), dblBuff);
            if(stXDisplay->hVisInfo == NULL) {
                myMaster.close();
                stError("X, no RGB visual with depth buffer");
                myInitState = STWIN_ERROR_X_NORGB;
                return false;
            }
        }
    } else {
        // find an appropriate visual
        // find an OpenGL-capable RGB visual with depth buffer
        stXDisplay->hVisInfo = glXChooseVisual(hDisplay, DefaultScreen(hDisplay), dblBuff);
        if(stXDisplay->hVisInfo == NULL) {
            myMaster.close();
            stError("X, no RGB visual with depth buffer");
            myInitState = STWIN_ERROR_X_NORGB;
            return false;
        }
    }
    if(myWinAttribs.isSlave) {
        // just copy handle
        mySlave.stXDisplay = stXDisplay;
    }

    // create an X window with the selected visual
    // create an X colormap since probably not using default visual
    aWinAttribsX.colormap = XCreateColormap(hDisplay,
                                            RootWindow(hDisplay, stXDisplay->getScreen()),
                                            stXDisplay->getVisual(), AllocNone);
    aWinAttribsX.border_pixel = 0;

    // what events we want to recive:
    aWinAttribsX.event_mask =  KeyPressMask   | KeyReleaseMask    // receive keyboard events
                            | ButtonPressMask | ButtonReleaseMask // receive mouse events
                            | StructureNotifyMask;                // receive ConfigureNotify event on resize and move
                          //| ResizeRedirectMask                  // receive ResizeRequest event on resize (instead of common ConfigureNotify)
                          //| ExposureMask
                          //| EnterWindowMask|LeaveWindowMask
                          //| PointerMotionMask|PointerMotionHintMask|Button1MotionMask|Button2MotionMask|Button3MotionMask|Button4MotionMask|Button5MotionMask|ButtonMotionMask
                          //| KeymapStateMask|ExposureMask|VisibilityChangeMask
                          //| SubstructureNotifyMask|SubstructureRedirectMask
                          //| FocusChangeMask|PropertyChangeMask|ColormapChangeMask|OwnerGrabButtonMask

    updateChildRect();

    Window aParentWin = (Window )myParentWin.winHandle;
    if(aParentWin == 0 && !myWinAttribs.isNoDecor) {
        aWinAttribsX.override_redirect = False;
        myMaster.hWindow = XCreateWindow(hDisplay, stXDisplay->getRootWindow(),
                                         myRectNorm.left(),  myRectNorm.top(),
                                         myRectNorm.width(), myRectNorm.height(),
                                         0, stXDisplay->getDepth(),
                                         InputOutput,
                                         stXDisplay->getVisual(),
                                         CWBorderPixel | CWColormap | CWEventMask | CWOverrideRedirect, &aWinAttribsX);

        if(myMaster.hWindow == 0) {
            myMaster.close();
            stError("X, XCreateWindow failed for Master");
            myInitState = STWIN_ERROR_X_CREATEWIN;
            return false;
        }
        aParentWin = myMaster.hWindow;

        XSetStandardProperties(hDisplay, myMaster.hWindow,
                               myWindowTitle.toCString(),
                               myWindowTitle.toCString(),
                               None, NULL, 0, NULL);
    }

    aWinAttribsX.override_redirect = True; // GL window always undecorated
    myMaster.hWindowGl = XCreateWindow(hDisplay, (aParentWin != 0) ? aParentWin : stXDisplay->getRootWindow(),
                                       0, 0, myRectNorm.width(), myRectNorm.height(),
                                       0, stXDisplay->getDepth(),
                                       InputOutput,
                                       stXDisplay->getVisual(),
                                       CWBorderPixel | CWColormap | CWEventMask | CWOverrideRedirect, &aWinAttribsX);
    if(myMaster.hWindowGl == 0) {
        myMaster.close();
        stError("X, XCreateWindow failed for Master");
        myInitState = STWIN_ERROR_X_CREATEWIN;
        return false;
    }

    XSetStandardProperties(hDisplay, myMaster.hWindowGl,
                           "master window", "master window",
                           None, NULL, 0, NULL);

    if(myWinAttribs.isSlave) {
        aWinAttribsX.event_mask = NoEventMask; // we do not parse any events to slave window!
        aWinAttribsX.override_redirect = True; // slave window always undecorated
        mySlave.hWindowGl = XCreateWindow(hDisplay, stXDisplay->getRootWindow(),
                                          getSlaveLeft(),  getSlaveTop(),
                                          getSlaveWidth(), getSlaveHeight(),
                                          0, stXDisplay->getDepth(),
                                          InputOutput,
                                          stXDisplay->getVisual(),
                                          CWBorderPixel | CWColormap | CWEventMask | CWOverrideRedirect, &aWinAttribsX);

        if(mySlave.hWindowGl == 0) {
            myMaster.close();
            mySlave.close();
            stError("X, XCreateWindow failed for Slave");
            myInitState = STWIN_ERROR_X_CREATEWIN;
            return false;
        }

        XSetStandardProperties(hDisplay, mySlave.hWindowGl,
                               "slave window", "slave window",
                               None, NULL, 0, NULL);
    }

    int isGlCtx = myMaster.glCreateContext(myWinAttribs.isSlave ? &mySlave : NULL, myWinAttribs.isGlStereo);
    if(isGlCtx != STWIN_INIT_SUCCESS) {
        myMaster.close();
        mySlave.close();
        myInitState = isGlCtx;
        return false;
    }

    // handle close window event
    if(myMaster.hWindow != 0) {
        XSetWMProtocols(hDisplay, myMaster.hWindow, &(stXDisplay->wndDestroyAtom), 1);
    }

    // Announce XDND support
    Atom version = 5;
    XChangeProperty(hDisplay, myMaster.hWindowGl, stXDisplay->xDNDAware, XA_ATOM, 32, PropModeReplace, (unsigned char* )&version, 1);
    if(myMaster.hWindow != 0) {
        XChangeProperty(hDisplay, myMaster.hWindow, stXDisplay->xDNDAware, XA_ATOM, 32, PropModeReplace, (unsigned char* )&version, 1);
    }

    // Initialize XRandr events reception
    if(XRRQueryExtension(hDisplay, &myMaster.xrandrEventBase, &dummy)) {
        XRRSelectInput(hDisplay,
                       stXDisplay->getRootWindow(),
                       RRScreenChangeNotifyMask |
                       RRCrtcChangeNotifyMask   |
                       RROutputPropertyNotifyMask);
        myMaster.isRecXRandrEvents = true;
    } else {
        myMaster.isRecXRandrEvents = false;
    }

    // request the X window to be displayed on the screen
    if(myWinAttribs.isSlave) {

        // request the X window to be displayed on the screen
        if(!myWinAttribs.isSlaveHide && (!isSlaveIndependent() || myMonitors.size() > 1)) {
            XMapWindow(hDisplay, mySlave.hWindowGl);
            //XIfEvent(hDisplay, &myXEvent, stXWaitMapped, (char* )mySlave.hWindowGl);
        }
        // always hise mouse cursor on slave window
        const char noPixData[] = {0, 0, 0, 0, 0, 0, 0, 0};
        XColor black, dummy;
        Colormap cmap = DefaultColormap(hDisplay, DefaultScreen(hDisplay));
        XAllocNamedColor(hDisplay, cmap, "black", &black, &dummy);
        Pixmap noPix = XCreateBitmapFromData(hDisplay, mySlave.hWindowGl, noPixData, 8, 8);
        Cursor noPtr = XCreatePixmapCursor(hDisplay, noPix, noPix, &black, &black, 0, 0);
        XDefineCursor(hDisplay, mySlave.hWindowGl, noPtr);
        XFreeCursor(hDisplay, noPtr);
        if(noPix != None) {
            XFreePixmap(hDisplay, noPix);
        }
        XFreeColors(hDisplay, cmap, &black.pixel, 1, 0);
    }
    if(!myWinAttribs.isHide) {
        if(myMaster.hWindow != 0) {
            XMapWindow(hDisplay, myMaster.hWindow);
            //XIfEvent(hDisplay, &myXEvent, stXWaitMapped, (char* )myMaster.hWindow);
        }
        XMapWindow(hDisplay, myMaster.hWindowGl);
        //XIfEvent(hDisplay, &myXEvent, stXWaitMapped, (char* )myMaster.hWindowGl);
    }

    // setup default icon
    if((Window )myParentWin.winHandle == 0) {
        XpmCreatePixmapFromData(hDisplay, myMaster.hWindow, (char** )sview_xpm, &myMaster.iconImage, &myMaster.iconShape, NULL);
        XWMHints anIconHints;
        anIconHints.flags       = IconPixmapHint | IconMaskHint;
        anIconHints.icon_pixmap = myMaster.iconImage;
        anIconHints.icon_mask   = myMaster.iconShape;
        XSetWMHints(hDisplay, myMaster.hWindow, &anIconHints);
    }

    // we need this call to go around bugs
    if(!myWinAttribs.isFullScreen && myMaster.hWindow != 0) {
        XMoveResizeWindow(hDisplay, myMaster.hWindow,
                          myRectNorm.left(),  myRectNorm.top(),
                          myRectNorm.width(), myRectNorm.height());
    }
    // flushes the output buffer, most client apps needn't use this cause buffer is automatically flushed as needed by calls to XNextEvent()...
    XFlush(hDisplay);
    myIsUpdated = true;
    myInitState = STWIN_INIT_SUCCESS;
    return true;
}

/**
 * Update StWindow position according to native parent position.
 */
void StWindowImpl::updateChildRect() {
    if(!myWinAttribs.isFullScreen && (Window )myParentWin.winHandle != 0 && !myMaster.stXDisplay.isNull()) {
        Display* hDisplay = myMaster.stXDisplay->hDisplay;
        Window dummyWin;
        int xReturn, yReturn;
        unsigned int widthReturn, heightReturn, uDummy;
        XGetGeometry(hDisplay, (Window )myParentWin.winHandle, &dummyWin,
                     &xReturn, &yReturn, &widthReturn, &heightReturn,
                     &uDummy, &uDummy);
        XTranslateCoordinates(hDisplay, (Window )myParentWin.winHandle,
                              myMaster.stXDisplay->getRootWindow(),
                              0, 0, &myRectNorm.left(), &myRectNorm.top(), &dummyWin);
        myRectNorm.right()  = myRectNorm.left() + widthReturn;
        myRectNorm.bottom() = myRectNorm.top() + heightReturn;

        // update only when changes
        if(myRectNorm != myRectNormPrev) {
            myRectNormPrev = myRectNorm;
            myIsUpdated    = true;
            myMessageList.append(StMessageList::MSG_RESIZE);

            /// TODO (Kirill Gavrilov#4) remove this workaround!
            // call this to be sure master window showed well
            // this useful only when compiz on - seems to be bug in it!
            if(myReparentHackX && myMaster.hWindowGl != 0) {
                XReparentWindow(hDisplay, myMaster.hWindowGl, (Window )myParentWin.winHandle, 0, 0);
            }
        }
    }
}

void StWindowImpl::setFullScreen(bool theFullscreen) {
    if(myWinAttribs.isFullScreen != theFullscreen) {
        myWinAttribs.isFullScreen = theFullscreen;
        if(myWinAttribs.isFullScreen) {
            myFullScreenWinNb.increment();
        } else {
            myFullScreenWinNb.decrement();
        }
    }

    if(myWinAttribs.isHide) {
        // TODO (Kirill Gavrilov#9) parse correctly
        // do nothing, just set the flag
        return;
    } else if(myMaster.stXDisplay.isNull()) {
        return;
    }

    Display* hDisplay = myMaster.stXDisplay->hDisplay;
    if(myWinAttribs.isFullScreen) {
        const StMonitor& stMon = (myMonMasterFull == -1) ? myMonitors[myRectNorm.center()] : myMonitors[myMonMasterFull];
        myRectFull = stMon.getVRect();
        XUnmapWindow(hDisplay, myMaster.hWindowGl); // workaround for strange bugs

        if((Window )myParentWin.winHandle != 0 || myMaster.hWindow != 0) {
            XReparentWindow(hDisplay, myMaster.hWindowGl, myMaster.stXDisplay->getRootWindow(), 0, 0);
            XMoveResizeWindow(hDisplay, myMaster.hWindowGl,
                              myRectFull.left(),  myRectFull.top(),
                              myRectFull.width(), myRectFull.height());
            XFlush(hDisplay);
            XMapWindow(hDisplay, myMaster.hWindowGl);
            //XIfEvent(hDisplay, &myXEvent, stXWaitMapped, (char* )myMaster.hWindowGl);
        } else {
            XMoveResizeWindow(hDisplay, myMaster.hWindowGl,
                              myRectFull.left(),  myRectFull.top(),
                              myRectFull.width(), myRectFull.height());
            XFlush(hDisplay);
            XMapWindow(hDisplay, myMaster.hWindowGl);
            //XIfEvent(hDisplay, &myXEvent, stXWaitMapped, (char* )myMaster.hWindowGl);
        }

        if(myWinAttribs.isSlave  && (!isSlaveIndependent() || myMonitors.size() > 1)) {
            XMoveResizeWindow(hDisplay, mySlave.hWindowGl,
                              getSlaveLeft(),  getSlaveTop(),
                              getSlaveWidth(), getSlaveHeight());
        }
    } else {
        Window aParent = ((Window )myParentWin.winHandle != 0) ? (Window )myParentWin.winHandle : myMaster.hWindow;
        if(aParent != 0) {
            XReparentWindow(hDisplay, myMaster.hWindowGl, aParent, 0, 0);
            myIsUpdated = true;
        } else {
            XUnmapWindow(hDisplay, myMaster.hWindowGl); // workaround for strange bugs
            XResizeWindow(hDisplay, myMaster.hWindowGl, 256, 256);
            if(myWinAttribs.isSlave && (!isSlaveIndependent() || myMonitors.size() > 1)) {
                XUnmapWindow (hDisplay, mySlave.hWindowGl);
                XResizeWindow(hDisplay, mySlave.hWindowGl, 256, 256);
            }
            XFlush(hDisplay);
            XMapWindow(hDisplay, myMaster.hWindowGl);
            //XIfEvent(hDisplay, &myXEvent, stXWaitMapped, (char* )myMaster.hWindowGl);
            if(myWinAttribs.isSlave && (!isSlaveIndependent() || myMonitors.size() > 1)) {
                XMapWindow(hDisplay, mySlave.hWindowGl);
                //XIfEvent(hDisplay, &myXEvent, stXWaitMapped, (char* )mySlave.hWindowGl);
            }
            XFlush(hDisplay);
            XMoveResizeWindow(hDisplay, myMaster.hWindowGl,
                              myRectNorm.left(),  myRectNorm.top(),
                              myRectNorm.width(), myRectNorm.height());
        }
    }
    XSetInputFocus(hDisplay, myMaster.hWindowGl, RevertToParent, CurrentTime);

    myMessageList.append(StMessageList::MSG_RESIZE); // add event to update GL rendering scape
    myMessageList.append(StMessageList::MSG_FULLSCREEN_SWITCH);
    // flushes the output buffer, most client apps needn't use this cause buffer is automatically flushed as needed by calls to XNextEvent()...
    XFlush(hDisplay);
}

void StWindowImpl::parseXDNDClientMsg() {
    const StXDisplayH& aDisplay = myMaster.stXDisplay;
    // myMaster.hWindow or myMaster.hWindowGl
    Window aWinReciever = ((XClientMessageEvent* )&myXEvent)->window;
    if(myXEvent.xclient.message_type == aDisplay->xDNDEnter) {
        myMaster.xDNDVersion = (myXEvent.xclient.data.l[1] >> 24);
        bool isMoreThan3 = myXEvent.xclient.data.l[1] & 1;
        Window aSrcWin = myXEvent.xclient.data.l[0];
        /*ST_DEBUG_LOG(
            "Xdnd: Source window = 0x" + (int )myXEvent.xclient.data.l[0] + "\n"
            + "Supports > 3 types = " + (more_than_3) + "\n"
            + "Protocol version = " + myMaster.xDNDVersion + "\n"
            + "Type 1 = " + myMaster.getAtomName(myXEvent.xclient.data.l[2]) + "\n"
            + "Type 2 = " + myMaster.getAtomName(myXEvent.xclient.data.l[3]) + "\n"
            + "Type 3 = " + myMaster.getAtomName(myXEvent.xclient.data.l[4]) + "\n"
        );*/
        if(isMoreThan3) {
            Property aProperty = aDisplay->readProperty(aSrcWin, aDisplay->xDNDTypeList);
            Atom* anAtomList = (Atom* )aProperty.data;
            for(int anIter = 0; anIter < aProperty.nitems; ++anIter) {
                if(anAtomList[anIter] == aDisplay->xDNDPlainText) {
                    myMaster.xDNDRequestType = aDisplay->xDNDPlainText;
                    break;
                }
            }
            XFree(aProperty.data);
        } else if((Atom )myXEvent.xclient.data.l[2] == aDisplay->xDNDPlainText
               || (Atom )myXEvent.xclient.data.l[3] == aDisplay->xDNDPlainText
               || (Atom )myXEvent.xclient.data.l[4] == aDisplay->xDNDPlainText) {
            myMaster.xDNDRequestType = aDisplay->xDNDPlainText;
        } else {
            myMaster.xDNDRequestType = XA_STRING;
        }
    } else if(myXEvent.xclient.message_type == aDisplay->xDNDPosition) {
        /*ST_DEBUG_LOG(
            "Source window = 0x" + (int )myXEvent.xclient.data.l[0] + "\n"
            + "Position: x=" + (int )(myXEvent.xclient.data.l[2] >> 16) + " y=" + (int )(myXEvent.xclient.data.l[2] & 0xffff)  + "\n"
            + "Timestamp = " + (int )myXEvent.xclient.data.l[3] + " (Version >= 1 only)\n"
        );*/

        // Xdnd: reply with an XDND status message
        XClientMessageEvent aMsg;
        stMemSet(&aMsg, 0, sizeof(aMsg));
        aMsg.type    = ClientMessage;
        aMsg.display = myXEvent.xclient.display;
        aMsg.window  = myXEvent.xclient.data.l[0];
        aMsg.message_type = aDisplay->xDNDStatus;
        aMsg.format  = 32;
        aMsg.data.l[0] = aWinReciever;
        aMsg.data.l[1] = True; //
        aMsg.data.l[2] = 0;    // specify an empty rectangle
        aMsg.data.l[3] = 0;
        aMsg.data.l[4] = aDisplay->xDNDActionCopy; // we only accept copying anyway.

        XSendEvent(aDisplay->hDisplay, myXEvent.xclient.data.l[0], False, NoEventMask, (XEvent* )&aMsg);
        XFlush(aDisplay->hDisplay);
    } else if(myXEvent.xclient.message_type == aDisplay->xDNDLeave) {
        //
    } else if(myXEvent.xclient.message_type == aDisplay->xDNDDrop) {
        myMaster.xDNDSrcWindow = myXEvent.xclient.data.l[0];
        Atom aSelection = XInternAtom(aDisplay->hDisplay, "PRIMARY", 0);
        if(myMaster.xDNDVersion >= 1) {
            XConvertSelection(aDisplay->hDisplay, aDisplay->xDNDSelection, myMaster.xDNDRequestType,
                              aSelection, aWinReciever, myXEvent.xclient.data.l[2]);
        } else {
            XConvertSelection(aDisplay->hDisplay, aDisplay->xDNDSelection, myMaster.xDNDRequestType,
                              aSelection, aWinReciever, CurrentTime);
        }
    }
}

void StWindowImpl::parseXDNDSelectionMsg() {
    Atom aTarget = myXEvent.xselection.target;
    const StXDisplayH& aDisplay = myMaster.stXDisplay;
    // myMaster.hWindow or myMaster.hWindowGl
    Window aWinReciever = ((XClientMessageEvent* )&myXEvent)->window;
    /*ST_DEBUG_LOG(
        "A selection notify has arrived!\n"
        + "Requestor = 0x" + (int )myXEvent.xselectionrequest.requestor + "\n"
        + "Selection atom = " + myMaster.getAtomName(myXEvent.xselection.selection) + "\n"
        + "Target atom    = " + myMaster.getAtomName(aTarget) + "\n"
        + "Property atom  = " + myMaster.getAtomName(myXEvent.xselection.property) + "\n"
    );*/
    if(myXEvent.xselection.property == None) {
        return;
    } else {
        Atom aSelection = XInternAtom(aDisplay->hDisplay, "PRIMARY", 0);
        Property aProperty = aDisplay->readProperty(aWinReciever, aSelection);
        //If we're being given a list of targets (possible conversions)
        if(aTarget == aDisplay->XA_TARGETS) {
            XConvertSelection(aDisplay->hDisplay, aSelection, XA_STRING, aSelection, aWinReciever, CurrentTime);
        } else if(aTarget == myMaster.xDNDRequestType) {
            StString aData = StString((char* )aProperty.data);

            myDndMutex.lock();
            myDndCount = 1;
            delete[] myDndList;
            myDndList = new StString[1];

            size_t anEndChar = aData.getLength();
            for(StUtf8Iter anIter = aData.iterator(); *anIter != 0; ++anIter) {
                // cut only first filename, separated with CR/LF
                if(*anIter == stUtf32_t('\n') || *anIter == stUtf32_t(13)) {
                    anEndChar = anIter.getIndex();
                    break;
                }
            }

            // TODO (Kirill Gavrilov#5#) improve functionality
            // notes:
            // 1) dragndrop can move not only strings
            // 2) filename converted to links (maybe non-local!)
            const StString ST_FILE_PROTOCOL("file://");
            size_t aCutFrom = aData.isStartsWith(ST_FILE_PROTOCOL) ? ST_FILE_PROTOCOL.getLength() : 0;
            aData = aData.subString(aCutFrom, anEndChar);
            if(myMaster.xDNDRequestType != XA_STRING) {
                myDndList[0].fromUrl(aData);
            } else {
                myDndList[0] = aData;
            }
            myDndMutex.unlock();
            myMessageList.append(StMessageList::MSG_DRAGNDROP_IN);
            //ST_DEBUG_LOG(data);

            // Reply OK
            XClientMessageEvent aMsg;
            stMemSet(&aMsg, 0, sizeof(aMsg));
            aMsg.type      = ClientMessage;
            aMsg.display   = aDisplay->hDisplay;
            aMsg.window    = myMaster.xDNDSrcWindow;
            aMsg.message_type = aDisplay->xDNDFinished;
            aMsg.format    = 32;
            aMsg.data.l[0] = aWinReciever;
            aMsg.data.l[1] = 1;
            aMsg.data.l[2] = aDisplay->xDNDActionCopy;

            // Reply that all is well
            XSendEvent(aDisplay->hDisplay, myMaster.xDNDSrcWindow, False, NoEventMask, (XEvent* )&aMsg);
            XSync(aDisplay->hDisplay, False);
        }
        XFree(aProperty.data);
    }
}

void StWindowImpl::updateWindowPos() {
    const StXDisplayH& aDisplay = myMaster.stXDisplay;
    if(aDisplay.isNull() || myMaster.hWindowGl == 0) {
        return;
    }
    if(!myWinAttribs.isFullScreen) {
        if(myRectNorm.left() == 0 && myRectNorm.top() == 0 && myMaster.hWindow != 0) {
            int width  = myRectNorm.width();
            int height = myRectNorm.height();
            Window hChildWin;
            XTranslateCoordinates(aDisplay->hDisplay, myMaster.hWindow,
                                  aDisplay->getRootWindow(),
                                  0, 0, &myRectNorm.left(), &myRectNorm.top(), &hChildWin);
            myRectNorm.right()  = myRectNorm.left() + width;
            myRectNorm.bottom() = myRectNorm.top() + height;
        }
        if(myMaster.hWindow != 0) {
            XMoveResizeWindow(aDisplay->hDisplay, myMaster.hWindowGl,
                              0, 0,
                              myRectNorm.width(), myRectNorm.height());
        }
        if(myWinAttribs.isSlave && (!isSlaveIndependent() || myMonitors.size() > 1)) {
            XMoveResizeWindow(aDisplay->hDisplay, mySlave.hWindowGl,
                              getSlaveLeft(),  getSlaveTop(),
                              getSlaveWidth(), getSlaveHeight());
        }
    }
    myMessageList.append(StMessageList::MSG_RESIZE); // add event to update GL rendering scape

    // force input focus to Master
    XSetInputFocus(aDisplay->hDisplay, myMaster.hWindowGl, RevertToParent, CurrentTime);

    // detect when window moved to another monitor
    if(!myWinAttribs.isFullScreen && myMonitors.size() > 1) {
        int aNewMonId = myMonitors[myRectNorm.center()].getId();
        if(myWinOnMonitorId != aNewMonId) {
            myMessageList.append(StMessageList::MSG_WIN_ON_NEW_MONITOR);
            myWinOnMonitorId = aNewMonId;
        }
    }
}

// Function set to argument-buffer given events
void StWindowImpl::callback(StMessage_t* theMessages) {
    // get callback from Master window
    const StXDisplayH& aDisplay = myMaster.stXDisplay;
    if(!aDisplay.isNull()) {
        // detect embedded window was moved
        if(!myWinAttribs.isFullScreen && (Window )myParentWin.winHandle != 0 && myMaster.hWindowGl != 0
        //&&  myWinAttribs.isSlave
        //&& !myWinAttribs.isSlaveHLineTop && !myWinAttribs.isSlaveHTop2Px && !myWinAttribs.isSlaveHLineBottom
        && ++mySyncCounter > 4) {
            // sadly, this Xlib call takes a lot of time
            // perform it rarely
            Window aDummyWin;
            int aCurrPosX, aCurrPosY;
            if(XTranslateCoordinates(aDisplay->hDisplay, myMaster.hWindowGl,
                                     aDisplay->getRootWindow(),
                                     0, 0, &aCurrPosX, &aCurrPosY, &aDummyWin)
            && (aCurrPosX != myRectNorm.left() || aCurrPosY != myRectNorm.top())) {
                //ST_DEBUG_LOG("need updateChildRect= " + aCurrPosX + " x " + aCurrPosY);
                updateChildRect();
            }
            mySyncCounter = 0;
        }

        int anEventsNb = XPending(aDisplay->hDisplay);
        for(int anIter = 0; anIter < anEventsNb && XPending(aDisplay->hDisplay) > 0; ++anIter) {
            XNextEvent(aDisplay->hDisplay, &myXEvent);
            switch(myXEvent.type) {
                case ClientMessage: {
                    /*ST_DEBUG_LOG("A ClientMessage has arrived:\n"
                        + "Type = " + aDisplay->getAtomName(myXEvent.xclient.message_type)
                        + " (" + myXEvent.xclient.format + ")\n"
                        + " message_type " + myXEvent.xclient.message_type + "\n"
                    );*/
                    parseXDNDClientMsg();
                    if(myXEvent.xclient.data.l[0] == (int )aDisplay->wndDestroyAtom) {
                        myMessageList.append(StMessageList::MSG_CLOSE);
                    }
                    break;
                }
                case SelectionNotify: {
                    parseXDNDSelectionMsg();
                    break;
                }
                case DestroyNotify: {
                    // something else...
                    break;
                }
                case KeyPress: {
                    XKeyEvent* aKeyEvent = (XKeyEvent* )&myXEvent;
                    // It is necessary to convert the keycode to a keysym
                    KeySym aKeySym = XLookupKeysym(aKeyEvent, 0);
                    /**ST_DEBUG_LOG("KeyPress,   keycode= " + aKeyEvent->keycode
                             + "; KeySym = " + (unsigned int )aKeySym + "\n");*/
                    if(aKeySym >= ST_XK2ST_VK_SIZE) {
                        ST_DEBUG_LOG("KeyPress,   keycode= " + aKeyEvent->keycode
                                 + "; KeySym = " + (unsigned int )aKeySym + " ignored!\n");
                        break;
                    }
                    myMessageList.getKeysMap()[ST_XK2ST_VK[aKeySym]] = true;
                    break;
                }
                case KeyRelease: {
                    XKeyEvent* aKeyEvent = (XKeyEvent* )&myXEvent;
                    KeySym aKeySym = XLookupKeysym(aKeyEvent, 0);
                    /**ST_DEBUG_LOG("KeyRelease, keycode= " + aKeyEvent->keycode
                             + "; KeySym = " + (unsigned int )aKeySym + "\n");*/
                    if(aKeySym >= ST_XK2ST_VK_SIZE) {
                        ST_DEBUG_LOG("KeyRelease, keycode= " + aKeyEvent->keycode
                                 + "; KeySym = " + (unsigned int )aKeySym + " ignored!\n");
                        break;
                    }
                    myMessageList.getKeysMap()[ST_XK2ST_VK[aKeySym]] = false;
                    break;
                }
                case ButtonPress:
                case ButtonRelease: {
                    const XButtonEvent* aBtnEvent = &myXEvent.xbutton;
                    StPointD_t point(double(aBtnEvent->x) / double(getPlacement().width()),
                                     double(aBtnEvent->y) / double(getPlacement().height()));
                    int aMouseBtn = ST_NOMOUSE;
                    // TODO (Kirill Gavrilov#6#) parse extra mouse buttons
                    switch(aBtnEvent->button) {
                        case 1:  aMouseBtn = ST_MOUSE_LEFT;     break;
                        case 3:  aMouseBtn = ST_MOUSE_RIGHT;    break;
                        case 2:  aMouseBtn = ST_MOUSE_MIDDLE;   break;
                        default: aMouseBtn = aBtnEvent->button; break;
                    }
                    switch(myXEvent.type) {
                        case ButtonRelease: {
                            myMUpQueue.push(point, aMouseBtn);
                            myMessageList.append(StMessageList::MSG_MOUSE_UP);
                            break;
                        }
                        case ButtonPress: {
                            myMDownQueue.push(point, aMouseBtn);
                            myMessageList.append(StMessageList::MSG_MOUSE_DOWN);
                            break;
                        }
                    }
                    /*ST_DEBUG_LOG(((myXEvent.type == ButtonPress) ? "ButtonPress " : "Release ")
                        + aMouseBtn + ", x= " + point.x + ", y= " + point.y
                    );*/
                    // force input focus to the master window
                    XSetInputFocus(aDisplay->hDisplay, myMaster.hWindowGl, RevertToParent, CurrentTime);
                    break;
                }
                //case ResizeRequest:
                //case ConfigureRequest:
                case ConfigureNotify: {
                    //ST_DEBUG_LOG("ConfigureNotify");
                    const XConfigureEvent* aCfgEvent = &myXEvent.xconfigure;
                    if(!myWinAttribs.isFullScreen && aCfgEvent->window == myMaster.hWindow) {
                        StRectI_t aNewRect;
                        if(aCfgEvent->x > 64 && aCfgEvent->y > 64) {
                            // we got real GLorigin position
                            // worked when we in Compiz mode and on MoveWindow
                            aNewRect.left()   = aCfgEvent->x;
                            aNewRect.right()  = aCfgEvent->x + aCfgEvent->width;
                            aNewRect.top()    = aCfgEvent->y;
                            aNewRect.bottom() = aCfgEvent->y + aCfgEvent->height;
                        } else {
                            // we got psevdo x and y (~window decorations?)
                            // on ResizeWindow without Compiz
                            Window aChild;
                            aNewRect.left() = 0;
                            aNewRect.top()  = 0;
                            XTranslateCoordinates(aDisplay->hDisplay, myMaster.hWindow,
                                                  aDisplay->getRootWindow(),
                                                  0, 0, &aNewRect.left(), &aNewRect.top(), &aChild);
                            aNewRect.right()  = aNewRect.left() + aCfgEvent->width;
                            aNewRect.bottom() = aNewRect.top()  + aCfgEvent->height;
                        }
                        if(myRectNorm != aNewRect) {
                            // new compiz send messages when placement not really changed
                            myRectNorm  = aNewRect;
                            myIsUpdated = true; // call updateWindowPos() to update position
                        }
                    }
                    break;
                }
                default: {
                    // process XRandr events
                    if(myMaster.isRecXRandrEvents) {
                        int xrandrEventType = myXEvent.type - myMaster.xrandrEventBase;
                        if(xrandrEventType == RRNotify || xrandrEventType == RRScreenChangeNotify) {
                            ST_DEBUG_LOG("XRandr update event");
                            myMonitors.init(); // reinitialize monitors list
                            for(size_t aMonIter = 0; aMonIter < myMonitors.size(); ++aMonIter) {
                                ST_DEBUG_LOG(myMonitors[aMonIter].toString()); // just debug output
                            }
                        }
                    }
                }
            }
        }
    }

    StPointD_t aNewMousePt = getMousePos();
    if(aNewMousePt.x() >= 0.0 && aNewMousePt.x() <= 1.0 && aNewMousePt.y() >= 0.0 && aNewMousePt.y() <= 1.0) {
        StPointD_t aDspl = aNewMousePt - myMousePt;
        if(std::abs(aDspl.x()) >= 0.0008 || std::abs(aDspl.y()) >= 0.0008) {
            myMessageList.append(StMessageList::MSG_MOUSE_MOVE);
        }
    }
    myMousePt = aNewMousePt;

    if(myIsUpdated) {
        // update position only when all messages are parsed
        updateWindowPos();
        myIsUpdated = false;
    }
    updateActiveState();

    // TODO (Kirill Gavrilov#5#) parse multimedia keys
    myMessageList.popList(theMessages);
}

#endif
