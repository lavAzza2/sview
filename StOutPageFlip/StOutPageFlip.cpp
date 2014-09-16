/**
 * Copyright © 2007-2014 Kirill Gavrilov <kirill@sview.ru>
 *
 * StOutPageFlip library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * StOutPageFlip library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include "StOutPageFlip.h"
#include "StOutPageFlipStrings.h"
#include "StVuzixSDK.h"
#include "StQuadBufferCheck.h"
#include "StDXNVWindow.h"
#include "StGLDeviceControl.h"

#include <StGL/StGLEnums.h>
#include <StGL/StGLContext.h>
#include <StGL/StGLArbFbo.h>
#include <StGLCore/StGLCore20.h>
#include <StGLStereo/StGLStereoFrameBuffer.h>
#include <StCore/StSearchMonitors.h>
#include <StSettings/StSettings.h>
#include <StSettings/StTranslations.h>
#include <StAV/StAVImage.h>
#include <StSys/StSys.h>
#include <stAssert.h>

namespace {

    static const char ST_OUT_PLUGIN_NAME[] = "StOutPageFlip";

    static const char ST_SETTING_ADVANCED[]   = "advanced";
    static const char ST_SETTING_WINDOWPOS[]  = "windowPos";
    static const char ST_SETTING_DEVICE_ID[]  = "deviceId";
    static const char ST_SETTING_QUADBUFFER[] = "quadBufferType";
}

StOutPageFlip::StOutDirect3D::StOutDirect3D()
: GlIoBuff(0),
  ActivateStep(0),
  IsActive(false),
  ToUsePBO(true) {
    //
}

StOutPageFlip::StOutDirect3D::~StOutDirect3D() {
    //
}

#ifndef _WIN32
/**
 * Just dummy GLSL program.
 */
class StOutPageFlip::StProgramQuad : public StGLProgram {
    //
};
#else
SV_THREAD_FUNCTION StOutPageFlip::StOutDirect3D::dxThreadFunction(void* theStOutD3d) {
    StOutPageFlip::StOutDirect3D* aStOutD3d = (StOutPageFlip::StOutDirect3D* )theStOutD3d;
    aStOutD3d->DxWindow->dxLoop();
    return SV_THREAD_RETURN 0;
}

/**
 * Just dummy GLSL program.
 */
class StOutPageFlip::StProgramQuad : public StGLProgram {

        public:

    StProgramQuad() : StGLProgram("StProgramQuad") {}
    StGLVarLocation getVVertexLoc()   const { return atrVVertexLoc; }
    StGLVarLocation getVTexCoordLoc() const { return atrVTexCoordLoc; }

    virtual bool init(StGLContext& theCtx) {
        const char VERTEX_SHADER[] =
           "attribute vec4 vVertex; \
            attribute vec2 vTexCoord; \
            varying vec2 fTexCoord; \
            void main(void) { \
                fTexCoord = vTexCoord; \
                gl_Position = vVertex; \
            }";

        const char FRAGMENT_SHADER[] =
           "uniform sampler2D texR, texL; \
            varying vec2 fTexCoord; \
            void main(void) { \
                gl_FragColor = texture2D(texR, fTexCoord); \
            }";

        StGLVertexShader aVertexShader(StGLProgram::getTitle());
        StGLAutoRelease aTmp1(theCtx, aVertexShader);
        aVertexShader.init(theCtx, VERTEX_SHADER);

        StGLFragmentShader aFragmentShader(StGLProgram::getTitle());
        StGLAutoRelease aTmp2(theCtx, aFragmentShader);
        aFragmentShader.init(theCtx, FRAGMENT_SHADER);
        if(!StGLProgram::create(theCtx)
           .attachShader(theCtx, aVertexShader)
           .attachShader(theCtx, aFragmentShader)
           .link(theCtx)) {
            return false;
        }

        atrVVertexLoc   = StGLProgram::getAttribLocation(theCtx, "vVertex");
        atrVTexCoordLoc = StGLProgram::getAttribLocation(theCtx, "vTexCoord");
        return atrVVertexLoc.isValid() && atrVTexCoordLoc.isValid();
    }

        private:

    StGLVarLocation atrVVertexLoc;
    StGLVarLocation atrVTexCoordLoc;

};

/**
 * Shared Direct3d / OpenGl quad buffer object.
 * Technically one FBO might be sufficient for rendering,
 * but would complicate communication between D3D/OpenGL threads.
 */
class StOutPageFlip::StGLDXFrameBuffer {

        public:

    StGLDXFrameBuffer()
    : mySizeX(0),
      mySizeY(0),
      myWglDxDevice(NULL),
      myWglD3dSurfL(NULL),
      myWglD3dSurfLShare(NULL),
      myWglD3dSurfR(NULL),
      myWglD3dSurfRShare(NULL),
      myGlFboL(0),
      myGlSurfL(0),
      myGlDepthL(0),
      myGlFboR(0),
      myGlSurfR(0),
      myGlDepthR(0),
      myNeedDepth(false) {}

    virtual ~StGLDXFrameBuffer() {
        ST_ASSERT_SLIP(!isValid(), "~StGLDXFrameBuffer()", return);
    }

    GLsizei getSizeX() const {
        return mySizeX;
    }

    GLsizei getSizeY() const {
        return mySizeY;
    }

    bool init(StGLContext& theCtx,
              void*        theD3dDevice) {
        release(theCtx);
        if(theCtx.extAll->wglDXOpenDeviceNV == NULL) {
            return false;
        }

        myWglDxDevice = theCtx.extAll->wglDXOpenDeviceNV(theD3dDevice);
        if(myWglDxDevice == NULL) {
            ST_ERROR_LOG("wglDXOpenDeviceNV() has failed!");
            return false;
        }

        theCtx.arbFbo->glGenFramebuffers (1, &myGlFboL);
        theCtx.arbFbo->glGenFramebuffers (1, &myGlFboR);
        theCtx.arbFbo->glGenRenderbuffers(1, &myGlDepthL);
        theCtx.arbFbo->glGenRenderbuffers(1, &myGlDepthR);
        return true;
    }

    bool resize(StGLContext&  theCtx,
                void*         theD3dSurfL,
                void*         theD3dSurfLShare,
                void*         theD3dSurfR,
                void*         theD3dSurfRShare,
                const GLsizei theSizeX,
                const GLsizei theSizeY) {
        if(mySizeX == theSizeX
        && mySizeY == theSizeY
        && myWglD3dSurfLShare == theD3dSurfLShare
        && myWglD3dSurfRShare == theD3dSurfRShare) {
            return true;
        }

        releaseSurfaces(theCtx);
        if(theD3dSurfL == NULL
        || theD3dSurfR == NULL) {
            return false;
        }

        if((theD3dSurfLShare != NULL && !theCtx.extAll->wglDXSetResourceShareHandleNV(theD3dSurfL, theD3dSurfLShare))
        || (theD3dSurfRShare != NULL && !theCtx.extAll->wglDXSetResourceShareHandleNV(theD3dSurfR, theD3dSurfRShare))) {
            // call will fail when called second time for the same surface
        }

        theCtx.core11fwd->glGenTextures(1, &myGlSurfL);
        theCtx.core11fwd->glGenTextures(1, &myGlSurfR);
        myWglD3dSurfL = theCtx.extAll->wglDXRegisterObjectNV(myWglDxDevice, theD3dSurfL, myGlSurfL, GL_TEXTURE_2D, WGL_ACCESS_WRITE_DISCARD_NV);
        myWglD3dSurfR = theCtx.extAll->wglDXRegisterObjectNV(myWglDxDevice, theD3dSurfR, myGlSurfR, GL_TEXTURE_2D, WGL_ACCESS_WRITE_DISCARD_NV);
        if(myWglD3dSurfL == NULL
        || myWglD3dSurfR == NULL) {
            DWORD anError = GetLastError();
            ST_ERROR_LOG("wglDXRegisterObjectNV(" + (size_t )theD3dSurfL + " (" + size_t(theD3dSurfLShare) + "), "
                                                  + (size_t )theD3dSurfR + " (" + size_t(theD3dSurfRShare) + ") has failed (" + (int )anError + ")!");
            releaseSurfaces(theCtx);
            return false;
        }
        ST_DEBUG_LOG("wglDXRegisterObjectNV(" + (size_t )theD3dSurfL + " (" + size_t(theD3dSurfLShare) + "), "
                                              + (size_t )theD3dSurfR + " (" + size_t(theD3dSurfRShare) + ")");

        mySizeX = theSizeX;
        mySizeY = theSizeY;
        myWglD3dSurfLShare = theD3dSurfLShare;
        myWglD3dSurfRShare = theD3dSurfRShare;

        // create RenderBuffer for depth buffer
        if(myNeedDepth) {
            theCtx.arbFbo->glBindRenderbuffer(GL_RENDERBUFFER, myGlDepthL);
            theCtx.arbFbo->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT,
                                                 theSizeX, theSizeY);
            theCtx.arbFbo->glBindRenderbuffer(GL_RENDERBUFFER, myGlDepthR);
            theCtx.arbFbo->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT,
                                                 theSizeX, theSizeY);
            theCtx.arbFbo->glBindRenderbuffer(GL_RENDERBUFFER, StGLFrameBuffer::NO_RENDERBUFFER);
        }
        return true;
    }

    void releaseSurfaces(StGLContext& theCtx) {
        if(theCtx.core11fwd->glIsTexture(myGlSurfL)) {
            theCtx.core11fwd->glDeleteTextures(1, &myGlSurfL);
        }
        if(theCtx.core11fwd->glIsTexture(myGlSurfR)) {
            theCtx.core11fwd->glDeleteTextures(1, &myGlSurfR);
        }

        myGlSurfL = 0;
        myGlSurfR = 0;

        if(myWglDxDevice == NULL) {
            return;
        }

        if(myWglD3dSurfL != NULL) {
            theCtx.extAll->wglDXUnregisterObjectNV(myWglDxDevice, myWglD3dSurfL);
            myWglD3dSurfL      = NULL;
            myWglD3dSurfLShare = NULL;
        }
        if(myWglD3dSurfR != NULL) {
            theCtx.extAll->wglDXUnregisterObjectNV(myWglDxDevice, myWglD3dSurfR);
            myWglD3dSurfR      = NULL;
            myWglD3dSurfRShare = NULL;
        }
    }

    virtual void release(StGLContext& theCtx) {
        releaseSurfaces(theCtx);
        if(myGlFboL != StGLFrameBuffer::NO_FRAMEBUFFER) {
            theCtx.extAll->glDeleteFramebuffers(1, &myGlFboL);
        }
        if(myGlFboR != StGLFrameBuffer::NO_FRAMEBUFFER) {
            theCtx.extAll->glDeleteFramebuffers(1, &myGlFboR);
        }
        if(myGlDepthL != StGLFrameBuffer::NO_RENDERBUFFER) {
            theCtx.arbFbo->glDeleteRenderbuffers(1, &myGlDepthL);
        }
        if(myGlDepthR != StGLFrameBuffer::NO_RENDERBUFFER) {
            theCtx.arbFbo->glDeleteRenderbuffers(1, &myGlDepthR);
        }
        myGlFboL   = StGLFrameBuffer::NO_FRAMEBUFFER;
        myGlFboR   = StGLFrameBuffer::NO_FRAMEBUFFER;
        myGlDepthL = StGLFrameBuffer::NO_RENDERBUFFER;
        myGlDepthR = StGLFrameBuffer::NO_RENDERBUFFER;
        if(myWglDxDevice != NULL) {
            theCtx.extAll->wglDXCloseDeviceNV(myWglDxDevice);
            ST_DEBUG_LOG("wglDXCloseDeviceNV()");
            myWglDxDevice = NULL;
        }
    }

    bool isValid() const {
        return myWglD3dSurfL != NULL;
    }

    void bindBufferL(StGLContext& theCtx) {
        if(myWglD3dSurfL == NULL) {
            return;
        }

        theCtx.extAll->wglDXLockObjectsNV(myWglDxDevice, 1, &myWglD3dSurfL);
        theCtx.stglBindFramebuffer(myGlFboL);
        theCtx.arbFbo->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, myGlSurfL, 0);
        if(myNeedDepth) {
            theCtx.arbFbo->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, myGlDepthL);
        }
    }

    void bindBufferR(StGLContext& theCtx) {
        if(myWglD3dSurfR == NULL) {
            return;
        }

        theCtx.extAll->wglDXLockObjectsNV(myWglDxDevice, 1, &myWglD3dSurfR);
        theCtx.stglBindFramebuffer(myGlFboR);
        theCtx.arbFbo->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, myGlSurfR, 0);
        if(myNeedDepth) {
            theCtx.arbFbo->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, myGlDepthR);
        }
    }

    void unbindBufferL(StGLContext& theCtx) {
        if(myWglD3dSurfL == NULL) {
            return;
        }

        theCtx.stglBindFramebuffer(StGLFrameBuffer::NO_FRAMEBUFFER);
        theCtx.extAll->wglDXUnlockObjectsNV(myWglDxDevice, 1, &myWglD3dSurfL);
    }

    void unbindBufferR(StGLContext& theCtx) {
        if(myWglD3dSurfR == NULL) {
            return;
        }

        theCtx.stglBindFramebuffer(StGLFrameBuffer::NO_FRAMEBUFFER);
        theCtx.extAll->wglDXUnlockObjectsNV(myWglDxDevice, 1, &myWglD3dSurfR);
    }

        private:

    GLsizei mySizeX;
    GLsizei mySizeY;

    HANDLE  myWglDxDevice;
    HANDLE  myWglD3dSurfL;
    HANDLE  myWglD3dSurfLShare;
    HANDLE  myWglD3dSurfR;
    HANDLE  myWglD3dSurfRShare;

    GLuint  myGlFboL;
    GLuint  myGlSurfL;
    GLuint  myGlDepthL;
    GLuint  myGlFboR;
    GLuint  myGlSurfR;
    GLuint  myGlDepthR;

    bool    myNeedDepth;

};

#endif // _WIN32

static StMonitor getHigestFreqMonitor(const StSearchMonitors& theMonitors) {
    size_t hfreqMon = 0;
    int hfreqMax = 0;
    for(size_t aMonIter = 0; aMonIter < theMonitors.size(); ++aMonIter) {
        const StMonitor& aMon = theMonitors[aMonIter];
        if(aMon.getFreqMax() > hfreqMax) {
            hfreqMax = aMon.getFreqMax();
            hfreqMon = aMonIter;
        }
    }
    return !theMonitors.isEmpty() ? theMonitors[hfreqMon] : StMonitor();
}

StString StOutPageFlip::getRendererAbout() const {
    return myAbout;
}

const char* StOutPageFlip::getRendererId() const {
    return ST_OUT_PLUGIN_NAME;
}

const char* StOutPageFlip::getDeviceId() const {
    switch(myDevice) {
        case DEVICE_VUZIX:    return "Vuzix";
        case DEVICE_SHUTTERS:
        default:              return "Shutters";
    }
}

bool StOutPageFlip::isLostDevice() const {
    return myToResetDevice || StWindow::isLostDevice();
}

bool StOutPageFlip::setDevice(const StString& theDevice) {
    const int aPrevValue = myDevice;
    if(theDevice == "Shutters") {
        myDevice = DEVICE_SHUTTERS;
    } else if(theDevice == "Vuzix") {
        myDevice = DEVICE_VUZIX;
    }
    return myDevice != aPrevValue;
}

void StOutPageFlip::getDevices(StOutDevicesList& theList) const {
    for(size_t anIter = 0; anIter < myDevices.size(); ++anIter) {
        theList.add(myDevices[anIter]);
    }
}

void StOutPageFlip::getOptions(StParamsList& theList) const {
    theList.add(params.QuadBuffer);
    theList.add(params.ToShowExtra);
}

void StOutPageFlip::initGlobalsAsync() {
    StQuadBufferCheck::initAsync();
#ifdef _WIN32
    StDXManager::initInfoAsync();
#endif
}

StOutPageFlip::StOutPageFlip(const StHandle<StResourceManager>& theResMgr,
                             const StNativeWin_t                theParentWindow)
: StWindow(theResMgr, theParentWindow),
  mySettings(new StSettings(ST_OUT_PLUGIN_NAME)),
  myLangMap(theResMgr, ST_OUT_PLUGIN_NAME),
  myDevice(DEVICE_AUTO),
  myToDrawStereo(false),
#ifdef _WIN32
  myIsVistaPlus(StSys::isVistaPlus()),
#endif
  myToResetDevice(false) {
    StWindow::signals.onAnotherMonitor = stSlot(this, &StOutPageFlip::doNewMonitor);
    const StSearchMonitors& aMonitors = StWindow::getMonitors();

    // about string
    StString& aTitle     = myLangMap.changeValueId(STTR_PLUGIN_TITLE,   "sView - PageFlip Output module");
    StString& aVerString = myLangMap.changeValueId(STTR_VERSION_STRING, "version");
    StString& aDescr     = myLangMap.changeValueId(STTR_PLUGIN_DESCRIPTION,
        "(C) 2007-2014 Kirill Gavrilov <kirill@sview.ru>\nOfficial site: www.sview.ru\n\nThis library distributed under LGPL3.0");
    myAbout = aTitle + '\n' + aVerString + ": " + StVersionInfo::getSDKVersionString() + "\n \n" + aDescr;

    // detect connected displays
    bool hasQuadBufferGl  = false;
    bool hasQuadBufferD3D = false;
    int aSupportLevelShutters = ST_DEVICE_SUPPORT_NONE;
    int aSupportLevelVuzix    = StVuzixSDK::isConnected(aMonitors) ? ST_DEVICE_SUPPORT_PREFER : ST_DEVICE_SUPPORT_NONE;
    const StMonitor aMon = getHigestFreqMonitor(aMonitors);
    if(aMon.getFreqMax() >= 110) {
        aSupportLevelShutters = ST_DEVICE_SUPPORT_HIGHT;
    }
#ifndef __APPLE__
    // actually almost always available on mac but... is it useful?
    hasQuadBufferGl = StQuadBufferCheck::isSupported();
#endif

#ifdef _WIN32
    hasQuadBufferD3D = StDXManager::getInfo(myDxInfo) // && !hasQuadBufferGl
                    && (myDxInfo.hasNvStereoSupport || myDxInfo.hasAqbsSupport);
#endif
    if(hasQuadBufferGl || hasQuadBufferD3D) {
        aSupportLevelShutters = ST_DEVICE_SUPPORT_FULL;
    }

    // devices list
    StHandle<StOutDevice> aDevShutters = new StOutDevice();
    aDevShutters->PluginId = ST_OUT_PLUGIN_NAME;
    aDevShutters->DeviceId = "Shutters";
    aDevShutters->Priority = aSupportLevelShutters;
    aDevShutters->Name     = myLangMap.changeValueId(STTR_PAGEFLIP_NAME, "Shutter glasses");
    aDevShutters->Desc     = myLangMap.changeValueId(STTR_PAGEFLIP_DESC, "Shutter glasses");
    myDevices.add(aDevShutters);

    StHandle<StOutDevice> aDevVuzix = new StOutDevice();
    aDevVuzix->PluginId = ST_OUT_PLUGIN_NAME;
    aDevVuzix->DeviceId = "Vuzix";
    aDevVuzix->Priority = aSupportLevelVuzix;
    aDevVuzix->Name     = myLangMap.changeValueId(STTR_VUZIX_NAME, "Vuzix HMD");
    aDevVuzix->Desc     = myLangMap.changeValueId(STTR_VUZIX_DESC, "Vuzix HMD");
    myDevices.add(aDevVuzix);

    // load window position
    if(isMovable()) {
        StRect<int32_t> aRect(256, 768, 256, 1024);
        if(mySettings->loadInt32Rect(ST_SETTING_WINDOWPOS, aRect)) {
            StMonitor aMonitor = aMonitors[aRect.center()];
            if(!aMonitor.getVRect().isPointIn(aRect.center())) {
                ST_DEBUG_LOG("Warning, stored window position is out of the monitor(" + aMonitor.getId() + ")!" + aRect.toString());
                const int aWidth  = aRect.width();
                const int aHeight = aRect.height();
                aRect.left()   = aMonitor.getVRect().left() + 256;
                aRect.right()  = aRect.left() + aWidth;
                aRect.top()    = aMonitor.getVRect().top() + 256;
                aRect.bottom() = aRect.top() + aHeight;
            }
        } else {
            // try to open window on display with highest frequency
            aRect = aMon.getVRect();
            aRect.left()   = aRect.left() + 256;
            aRect.right()  = aRect.left() + 1024;
            aRect.top()    = aRect.top()  + 256;
            aRect.bottom() = aRect.top()  + 512;
        }
        StWindow::setPlacement(aRect);
    }

    // load device settings
    int aDeviceInt = int(myDevice);
    mySettings->loadInt32(ST_SETTING_DEVICE_ID, aDeviceInt);
    if(aDeviceInt == DEVICE_AUTO) {
        myDevice = DEVICE_SHUTTERS;
    } else {
        myDevice = DeviceEnum(aDeviceInt);
    }

    // Quad Buffer type option
    params.QuadBuffer = new StEnumParam(QUADBUFFER_HARD_OPENGL, myLangMap.changeValueId(STTR_PARAMETER_QBUFFER_TYPE, "Quad Buffer type"));
    params.QuadBuffer->signals.onChanged.connect(this, &StOutPageFlip::doSetQuadBuffer);
    params.QuadBuffer->changeValues().add(myLangMap.changeValueId(STTR_PARAMETER_QB_HARDWARE, "OpenGL Hardware"));
#ifdef _WIN32
    StString aDxDesc;
    if(myDxInfo.hasAqbsSupport && myDxInfo.hasNvStereoSupport) {
        aDxDesc = myLangMap.changeValueId(STTR_PARAMETER_QB_D3D_ANY,     "Direct3D (Fullscreen)");
    } else if(myDxInfo.hasAqbsSupport) {
        aDxDesc = myLangMap.changeValueId(STTR_PARAMETER_QB_D3D_AMD,     "Direct3D AMD (Fullscreen)");
    } else if(myDxInfo.hasNvStereoSupport) {
        aDxDesc = myLangMap.changeValueId(STTR_PARAMETER_QB_D3D_NV,      "Direct3D NVIDIA (Fullscreen)");
    } else if(myDxInfo.hasAmdAdapter) {
        aDxDesc = myLangMap.changeValueId(STTR_PARAMETER_QB_D3D_AMD_OFF, "Direct3D AMD (Unavailable)");
    } else if(myDxInfo.hasNvAdapter) {
        aDxDesc = myLangMap.changeValueId(STTR_PARAMETER_QB_D3D_NV_OFF,  "Direct3D NVIDIA (Disabled)");
    } else {
        aDxDesc = myLangMap.changeValueId(STTR_PARAMETER_QB_D3D_OFF,     "Direct3D (Unavailable)");
    }
    params.QuadBuffer->changeValues().add(aDxDesc);

    myOutD3d.Program = new StProgramQuad();
#endif

    // Show Extra option
    params.ToShowExtra = new StBoolParamNamed(false, "Show Extra Options");
    params.ToShowExtra->signals.onChanged.connect(this, &StOutPageFlip::doShowExtra);
    mySettings->loadParam(ST_SETTING_ADVANCED, params.ToShowExtra);

    // load Quad Buffer type
    if(!mySettings->loadParam(ST_SETTING_QUADBUFFER, params.QuadBuffer)) {
    #ifdef _WIN32
        if(!hasQuadBufferGl
         && hasQuadBufferD3D) {
            params.QuadBuffer->setValue(QUADBUFFER_HARD_D3D_ANY);
        }
    #endif
    }
    myToResetDevice = false;
}

void StOutPageFlip::releaseResources() {
    if(!myWarning.isNull()) {
        myWarning->release(*myContext);
        myWarning.nullify();
    }
#ifdef _WIN32
    if(!myContext.isNull()) {
        myOutD3d.Program->release(*myContext);
        myOutD3d.VertBuf.release(*myContext);
        myOutD3d.TCrdBuf.release(*myContext);
    }
#endif

    dxRelease();
    myContext.nullify();
    myVuzixSDK.nullify();

    // read windowed placement
    StWindow::hide();
    if(isMovable()) {
        StWindow::setFullScreen(false);
        mySettings->saveInt32Rect(ST_SETTING_WINDOWPOS, StWindow::getPlacement());
    }
    mySettings->saveInt32(ST_SETTING_DEVICE_ID,  myDevice);
    mySettings->saveParam(ST_SETTING_ADVANCED,   params.ToShowExtra);
    if(myWasUsed) {
        mySettings->saveParam(ST_SETTING_QUADBUFFER, params.QuadBuffer);
    }
}

StOutPageFlip::~StOutPageFlip() {
    releaseResources();
}

void StOutPageFlip::close() {
    StWindow::params.VSyncMode->signals.onChanged -= stSlot(this, &StOutPageFlip::doSwitchVSync);
    myToResetDevice = false;
    releaseResources();
    StWindow::close();
}

void StOutPageFlip::setupDevice() {
    switch(myDevice) {
        case DEVICE_VUZIX: {
            if(!StVuzixSDK::isConnected(StWindow::getMonitors())) {
                myMsgQueue->pushError(stCString("PageFlip output - Vuzix HMD Not Found!"));
                break;
            } else if(myVuzixSDK.isNull()) {
                myMsgQueue->pushError(stCString("PageFlip output - Failed to Load Vuzix VR920 Driver!"));
                break;
            }
            myVuzixSDK->open();
            break;
        }
        default: {
            if(!myVuzixSDK.isNull()) {
                myVuzixSDK->setMonoOut();
                myVuzixSDK->close();
            }
            break;
        }
    }
}

bool StOutPageFlip::dxInit() {
    dxRelease();
    myOutD3d.IsActive = false;
    myOutD3d.ActivateStep = 0;
#ifdef _WIN32
    StMonitor aNvMonitor = StWindow::getMonitors()[(StWindow::getPlacement().center())];
    GLsizei aFrBufferSizeX = aNvMonitor.getVRect().width();
    GLsizei aFrBufferSizeY = aNvMonitor.getVRect().height();

    // INIT framebuffers
    if(!myContext->arbNPTW) {
        StGLFrameBuffer::convertToPowerOfTwo(*myContext, aFrBufferSizeX, aFrBufferSizeY);
        myOutD3d.ToUsePBO = false; // force compatible mode
        ST_DEBUG_LOG(ST_OUT_PLUGIN_NAME + " Plugin, old videocard detected (GLSL 1.1)!");
    }

    myOutD3d.DxWindow = new StDXNVWindow(myMsgQueue, aFrBufferSizeX, aFrBufferSizeY, aNvMonitor, this,
                                         myContext->extAll->wglDXOpenDeviceNV != NULL);
    myOutD3d.DxThread = new StThread(StOutDirect3D::dxThreadFunction, (void* )&myOutD3d);
    return true;
#else
    return false;
#endif // _WIN32
}

void StOutPageFlip::dxRelease() {
#ifdef _WIN32
    dxDisactivate();
    if(!myOutD3d.DxThread.isNull()) {
        myOutD3d.DxWindow->quit();
        myOutD3d.DxThread->wait();
        myOutD3d.DxThread.nullify();
    }
    myOutD3d.DxWindow.nullify();
    if(!myOutD3d.GlBuffer.isNull()) {
        myOutD3d.GlBuffer->release(*myContext);
        myOutD3d.GlBuffer.nullify();
    }
    if(!myOutD3d.WglDxBuffer.isNull()) {
        myOutD3d.WglDxBuffer->release(*myContext);
        myOutD3d.WglDxBuffer.nullify();
    }

    if(myOutD3d.GlIoBuff != 0) {
        myContext->core20fwd->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        myContext->core20fwd->glDeleteBuffers(1, &myOutD3d.GlIoBuff);
        myOutD3d.GlIoBuff = 0;
    }
#endif
}

void StOutPageFlip::dxActivate() {
#ifdef _WIN32
    // activate Direct3D Fullscreen window
    if(!myOutD3d.IsActive
    && !myOutD3d.DxWindow.isNull()) {
        if(myOutD3d.ActivateStep == 0) {
            // Direct3D device will fail to Reset if GL fullscreen device is active
            // so we switch out main GL window into windowed state temporarily
            // (we need to wait some time to ensure system perform needed movements)
            StWindow::setFullScreen(false);
            StWindow::hide();
            ++myOutD3d.ActivateStep;
            return;
        }
        myOutD3d.DxWindow->waitReady();
        myOutD3d.DxWindow->show();

        myOutD3d.IsActive = true;
        if(myOutD3d.ActivateStep == 1) {
            // at second step switch out main GL window back to fullscreen
            myOutD3d.ActivateStep = 0;
            StWindow::setFullScreen(true);
            StWindow::hide();

            if(myOutD3d.WglDxBuffer.isNull()
            && myContext->extAll->wglDXOpenDeviceNV != NULL) {
                myOutD3d.DxWindow->lockLRBuffers();
                myOutD3d.WglDxBuffer = new StGLDXFrameBuffer();
                if(!myOutD3d.WglDxBuffer->init(*myContext, myOutD3d.DxWindow->getD3dManager()->getDevice())) {
                    myOutD3d.WglDxBuffer->release(*myContext);
                    myOutD3d.WglDxBuffer.nullify();
                }
                myOutD3d.DxWindow->unlockLRBuffers();
            }
            if(myOutD3d.GlBuffer.isNull()) {
                myOutD3d.GlBuffer = new StGLFrameBuffer();
                if(!myOutD3d.GlBuffer->init(*myContext, GL_RGBA8,
                                            (GLsizei )myOutD3d.DxWindow->getFboSizeX(), (GLsizei )myOutD3d.DxWindow->getFboSizeY(),
                                            StWindow::hasDepthBuffer())) {
                    myMsgQueue->pushError(stCString("PageFlip output - Failed to init OpenGL Frame Buffer!"));
                    myOutD3d.GlBuffer->release(*myContext);
                    myOutD3d.GlBuffer.nullify();
                    myOutD3d.WglDxBuffer->release(*myContext);
                    myOutD3d.WglDxBuffer.nullify();
                }
            }
        }

        if(!myOutD3d.VertBuf.isValid()) {
            const GLfloat QUAD_VERTICES[4 * 4] = {
                 1.0f, -1.0f, 0.0f, 1.0f, // top-right
                 1.0f,  1.0f, 0.0f, 1.0f, // bottom-right
                -1.0f, -1.0f, 0.0f, 1.0f, // top-left
                -1.0f,  1.0f, 0.0f, 1.0f  // bottom-left
            };
            const GLfloat QUAD_TEXCOORD[2 * 4] = {
                1.0f, 0.0f,
                1.0f, 1.0f,
                0.0f, 0.0f,
                0.0f, 1.0f
            };
            myOutD3d.VertBuf.init(*myContext, 4, 4, QUAD_VERTICES);
            myOutD3d.TCrdBuf.init(*myContext, 2, 4, QUAD_TEXCOORD);
            if(!myOutD3d.Program->init(*myContext)) {
                myMsgQueue->pushError(stCString("PageFlip output - critical error:\nShader initialization failed!"));
            }
        }
    }
#endif
}

void StOutPageFlip::dxDisactivate() {
#ifdef _WIN32
    if(myOutD3d.IsActive) {
        // set this flag first to make recursive calls
        myOutD3d.IsActive = false;
        if(!myOutD3d.DxWindow.isNull()) {
            myOutD3d.DxWindow->lockLRBuffers();
        }
        if(!myOutD3d.WglDxBuffer.isNull()) {
            myOutD3d.WglDxBuffer->release(*myContext);
            myOutD3d.WglDxBuffer.nullify();
        }
        if(!myOutD3d.DxWindow.isNull()) {
            myOutD3d.DxWindow->unlockLRBuffers();
        }

        if(!myOutD3d.DxWindow.isNull()) {
            myOutD3d.DxWindow->hide();
            StWindow::show(ST_WIN_MASTER);
        }

        // release unused resources
        if(!myOutD3d.GlBuffer.isNull()) {
            myOutD3d.GlBuffer->release(*myContext);
            myOutD3d.GlBuffer.nullify();
        }
        if(myOutD3d.GlIoBuff != 0) {
            myContext->core20fwd->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            myContext->core20fwd->glDeleteBuffers(1, &myOutD3d.GlIoBuff);
            myOutD3d.GlIoBuff = 0;
        }
    }
#endif
}

bool StOutPageFlip::create() {
    StWindow::show();

    // request Quad Buffer
    StWindow::setAttribute(StWinAttr_GlQuadStereo, params.QuadBuffer->getValue() == QUADBUFFER_HARD_OPENGL);
    if(!StWindow::create()) {
        return false;
    }

    // initialize GL context
    myContext = StWindow::getContext();
    myContext->setMessagesQueue(myMsgQueue);
    if(!myContext->isGlGreaterEqual(2, 0)) {
        myMsgQueue->pushError(stCString("OpenGL 2.0 is required by PageFlip Output"));
        return false;
    }

    switch(params.QuadBuffer->getValue()) {
        case QUADBUFFER_SOFT:
            myContext->stglSetVSync(StGLContext::VSync_ON);
            break; // VSync always on
        case QUADBUFFER_HARD_OPENGL: {
            GLboolean isStereoOn = GL_FALSE;
        #if !defined(GL_ES_VERSION_2_0)
            myContext->core20fwd->glGetBooleanv(GL_STEREO, &isStereoOn);
        #endif
            if(!isStereoOn) {
                myMsgQueue->pushError(myLangMap.changeValueId(STTR_NO_GL_QUADBUFFER,
                                                              "OpenGL Hardware QuadBuffer is unavailable!"));
            }
        }
        default:
            myContext->stglSetVSync((StGLContext::VSync_Mode )StWindow::params.VSyncMode->getValue());
            break;
    }

    StWindow::params.VSyncMode->signals.onChanged += stSlot(this, &StOutPageFlip::doSwitchVSync);

    // load fullscreen-only warning
    StAVImage anImage;
    StHandle<StResource> aWarnRes = getResourceManager()->getResource(StString("textures") + SYS_FS_SPLITTER + "pageflip_fullscreen.png");
    uint8_t* aData     = NULL;
    int      aDataSize = 0;
    if(!aWarnRes.isNull()
    && !aWarnRes->isFile()
    &&  aWarnRes->read()) {
        aData     = (uint8_t* )aWarnRes->getData();
        aDataSize = aWarnRes->getSize();
    }
    if(anImage.load(!aWarnRes.isNull() ? aWarnRes->getPath() : StString(), StImageFile::ST_TYPE_PNG, aData, aDataSize)) {
        myWarning = new StGLTexture(GL_RGBA8);
        if(!myWarning->init(*myContext, anImage.getPlane())) {
            ST_ERROR_LOG(ST_OUT_PLUGIN_NAME + " Plugin, Texture can not be initialized!");
            myWarning->release(*myContext);
            myWarning.nullify();
        }
    } else {
        ST_ERROR_LOG(ST_OUT_PLUGIN_NAME + " Plugin, Texture missed: " + anImage.getState());
    }

#ifdef _WIN32
    // initialize Direct3D output
    if(params.QuadBuffer->getValue() == QUADBUFFER_HARD_D3D_ANY) {
        dxInit();
    }
#endif

    // initialize Vuzix library
    myVuzixSDK = new StVuzixSDK();
    if(myVuzixSDK->init() != STERROR_LIBNOERROR) {
        myVuzixSDK.nullify();
        //ST_DEBUG_LOG(ST_OUT_PLUGIN_NAME + ST_STRING(" Plugin, Failed to Load Vuzix VR920 Driver!"));
    }

    setupDevice();
    return true;
}

void StOutPageFlip::doNewMonitor(const StSizeEvent& ) {
#ifdef _WIN32
    if(params.QuadBuffer->getValue() != QUADBUFFER_HARD_D3D_ANY
    || myOutD3d.IsActive) {
        return;
    }

    dxRelease();
    dxInit();
#endif
}

void StOutPageFlip::processEvents() {
    StWindow::processEvents();

    StKeysState& aKeys = StWindow::changeKeysState();
    if(aKeys.isKeyDown(ST_VK_F11)) {
        StWindow::stglSwap(ST_WIN_MASTER);
        aKeys.keyUp(ST_VK_F11, aKeys.getKeyTime(ST_VK_F11));
    }
}

void StOutPageFlip::dxDraw(unsigned int view) {
#ifdef _WIN32
    if(myOutD3d.ToUsePBO) {
        // use PBO to get some speed up
        // PBO macro (see spec for details)
        #define BUFFER_OFFSET(i) ((char *)NULL + (i))
        if(myOutD3d.GlIoBuff == 0) {
            myContext->core20fwd->glGenBuffers(1, &myOutD3d.GlIoBuff);
            myContext->core20fwd->glBindBuffer(GL_PIXEL_PACK_BUFFER, myOutD3d.GlIoBuff);
            myContext->core20fwd->glBufferData(GL_PIXEL_PACK_BUFFER, myOutD3d.GlBuffer->getSizeX() * myOutD3d.GlBuffer->getSizeY() * 4, NULL, GL_STREAM_READ);
        }

        myContext->core20fwd->glReadBuffer(GL_COLOR_ATTACHMENT0);
        myContext->core20fwd->glBindBuffer(GL_PIXEL_PACK_BUFFER, myOutD3d.GlIoBuff);
        myContext->core20fwd->glReadPixels(0, 0, myOutD3d.GlBuffer->getSizeX(), myOutD3d.GlBuffer->getSizeY(), GL_BGRA, GL_UNSIGNED_BYTE, BUFFER_OFFSET(0));
        GLubyte* ioMemBRGA = (GLubyte* )myContext->core20fwd->glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
        if(ioMemBRGA == NULL) {
            ST_DEBUG_LOG_AT("PBO buffer is sucks!");
        }

        if(view == ST_DRAW_LEFT) {
            myOutD3d.DxWindow->lockLRBuffers();
            myOutD3d.DxWindow->allocateBuffers();
            if(myOutD3d.DxWindow->getBuffLeft() != NULL) {
                stMemCpy(myOutD3d.DxWindow->getBuffLeft(),  ioMemBRGA, myOutD3d.GlBuffer->getSizeX() * myOutD3d.GlBuffer->getSizeY() * 4);
            }
        } else {
            if(myOutD3d.DxWindow->getBuffRight() != NULL) {
                stMemCpy(myOutD3d.DxWindow->getBuffRight(), ioMemBRGA, myOutD3d.GlBuffer->getSizeX() * myOutD3d.GlBuffer->getSizeY() * 4);
            }
            myOutD3d.DxWindow->unlockLRBuffers();
            myOutD3d.DxWindow->update();
        }
        myContext->core20fwd->glUnmapBuffer(GL_PIXEL_PACK_BUFFER); // release memory, i.e. give control back to the driver
        myContext->core20fwd->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    } else {
        // simple read
        if(view == ST_DRAW_LEFT) {
            myOutD3d.DxWindow->lockLRBuffers();
            myOutD3d.DxWindow->allocateBuffers();
            if(myOutD3d.DxWindow->getBuffLeft() != NULL) {
                myContext->core20fwd->glReadPixels(0, 0, myOutD3d.GlBuffer->getSizeX(), myOutD3d.GlBuffer->getSizeY(), GL_BGRA, GL_UNSIGNED_BYTE,
                                                   (GLubyte* )myOutD3d.DxWindow->getBuffLeft());
            }
        } else {
            if(myOutD3d.DxWindow->getBuffRight() != NULL) {
                myContext->core20fwd->glReadPixels(0, 0, myOutD3d.GlBuffer->getSizeX(), myOutD3d.GlBuffer->getSizeY(), GL_BGRA, GL_UNSIGNED_BYTE,
                                                   (GLubyte* )myOutD3d.DxWindow->getBuffRight());
            }
            myOutD3d.DxWindow->unlockLRBuffers();
            myOutD3d.DxWindow->update();
        }
    }
#endif
}

void StOutPageFlip::stglDrawWarning() {
    if(myWarning.isNull()) {
        return;
    }

#if !defined(GL_ES_VERSION_2_0)
    myContext->core20fwd->glDisable(GL_DEPTH_TEST);
    myContext->core20fwd->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    myContext->core20fwd->glEnable(GL_BLEND);
    myContext->core11->glEnable(GL_TEXTURE_2D);

    myWarning->bind(*myContext);

    const StGLBoxPx aVPort = StWindow::stglViewport(ST_WIN_MASTER);
    const int aWinSizeX = aVPort.width();
    const int aWinSizeY = aVPort.height();
    const GLfloat aWidth  = (aWinSizeX > 0) ?        GLfloat(myWarning->getSizeX()) / GLfloat(aWinSizeX) : 1.0f;
    const GLfloat aBottom = (aWinSizeY > 0) ? 100.0f / GLfloat(aWinSizeY) : 0.0f;
    const GLfloat aHeight = (aWinSizeY > 0) ? 2.0f * GLfloat(myWarning->getSizeY()) / GLfloat(aWinSizeY) : 1.0f;

    const GLfloat aVerts[] = {
         aWidth, -1.0f + aBottom + aHeight,
         aWidth, -1.0f + aBottom,
        -aWidth, -1.0f + aBottom + aHeight,
        -aWidth, -1.0f + aBottom,
    };

    const GLfloat aTCrds[] = {
        1.0f, 0.0f, // top-right
        1.0f, 1.0f, // bottom-right
        0.0f, 0.0f, // top-left
        0.0f, 1.0f  // bottom-left
    };

    myContext->core11->glLoadIdentity();

    myContext->core11->glEnableClientState(GL_VERTEX_ARRAY);
    myContext->core11->glVertexPointer(2, GL_FLOAT, 0, aVerts);
    myContext->core11->glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    myContext->core11->glTexCoordPointer(2, GL_FLOAT, 0, aTCrds);

    myContext->core11fwd->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    myContext->core11->glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    myContext->core11->glDisableClientState(GL_VERTEX_ARRAY);

    myWarning->unbind(*myContext);
    myContext->core11->glDisable(GL_TEXTURE_2D);
    myContext->core20fwd->glDisable(GL_BLEND);
#endif
}

void StOutPageFlip::stglDraw() {
    myFPSControl.setTargetFPS(StWindow::getTargetFps());

    StWindow::stglMakeCurrent(ST_WIN_MASTER);
    const StGLBoxPx aVPort = StWindow::stglViewport(ST_WIN_MASTER);
    myContext->stglResizeViewport(aVPort);

    if(!StWindow::isStereoOutput()) {
        // Vuzix driver control
        if(myToDrawStereo) {
            if(myDevice == DEVICE_VUZIX && !myVuzixSDK.isNull()) {
                myVuzixSDK->setMonoOut();
            }

            if(params.QuadBuffer->getValue() == QUADBUFFER_SOFT) {
                myContext->stglSetVSync((StGLContext::VSync_Mode )StWindow::params.VSyncMode->getValue());
            }

            myToDrawStereo = false;
        }

        // deactivate Direct3D Fullscreen window
        dxDisactivate();

        // setup whole back buffer (left+right) for hardware GL Quad Buffer
        if(params.QuadBuffer->getValue() == QUADBUFFER_HARD_OPENGL) {
        #if !defined(GL_ES_VERSION_2_0)
            myContext->core20fwd->glDrawBuffer(GL_BACK);
        #endif
        }

        // draw new MONO frame
        StWindow::signals.onRedraw(ST_DRAW_LEFT);
        if(myDevice != DEVICE_VUZIX) {
            stglDrawExtra(ST_DRAW_LEFT, StGLDeviceControl::OUT_MONO);
        }

        // decrease FPS to target by thread sleeps
        myFPSControl.sleepToTarget();
        StWindow::stglSwap(ST_WIN_MASTER);
        ++myFPSControl;
        return;
    } else if(!myToDrawStereo) {
        if(myDevice == DEVICE_VUZIX && !myVuzixSDK.isNull()
        && params.QuadBuffer->getValue() != QUADBUFFER_HARD_OPENGL) {
            myVuzixSDK->setStereoOut();
        }
        if(params.QuadBuffer->getValue() == QUADBUFFER_SOFT) {
            myContext->stglSetVSync(StGLContext::VSync_ON);
        }
        myToDrawStereo = true;
    }

    switch(params.QuadBuffer->getValue()) {
        case QUADBUFFER_HARD_OPENGL: {
            // We check capabilities at runtime to ensure that OpenGL context was created with Quad-Buffer.
            // Also we requires fullscreen for RadeOn cards on Windows 7.
            myContext->stglResetErrors(); // reset errors stack
            GLboolean isStereoOn = GL_FALSE;
        #if !defined(GL_ES_VERSION_2_0)
            myContext->core20fwd->glGetBooleanv(GL_STEREO, &isStereoOn);
            if(isStereoOn) {
                myContext->core20fwd->glDrawBuffer(GL_BACK_RIGHT);
                isStereoOn = (myContext->core20fwd->glGetError() == GL_NO_ERROR);
            }
        #endif

            if(!isStereoOn) {
            #if !defined(GL_ES_VERSION_2_0)
                myContext->core20fwd->glDrawBuffer(GL_BACK);
            #endif
                StWindow::signals.onRedraw(ST_DRAW_RIGHT);
                StWindow::signals.onRedraw(ST_DRAW_LEFT);
                stglDrawWarning();
            } else {
            #if !defined(GL_ES_VERSION_2_0)
                myContext->core20fwd->glDrawBuffer(GL_BACK_LEFT);
                StWindow::signals.onRedraw(ST_DRAW_LEFT);
                stglDrawExtra(ST_DRAW_LEFT, StGLDeviceControl::OUT_STEREO);

                myContext->stglResizeViewport(aVPort);
                myContext->core20fwd->glDrawBuffer(GL_BACK_RIGHT);
                StWindow::signals.onRedraw(ST_DRAW_RIGHT);
                stglDrawExtra(ST_DRAW_RIGHT, StGLDeviceControl::OUT_STEREO);
            #endif
            }
            StThread::sleep(1);
            StWindow::stglSwap(ST_WIN_MASTER);
            ++myFPSControl;
            return;
        }
    #ifdef _WIN32
        case QUADBUFFER_HARD_D3D_ANY: {
            if(myOutD3d.ActivateStep == 1 || StWindow::isFullScreen()) {
                dxActivate();

                // draw into virtual frame buffers (textures)
                if(!myOutD3d.WglDxBuffer.isNull()) {
                    while(myOutD3d.DxWindow->isInUpdate()) {
                        StThread::sleep(2);
                    }
                    myOutD3d.DxWindow->lockLRBuffers();
                    myOutD3d.DxWindow->update();

                    const StHandle<StDXNVSurface>& aSurf = myOutD3d.DxWindow->getD3dSurface();
                    if(aSurf.isNull()) {
                        myOutD3d.DxWindow->unlockLRBuffers();
                        break;
                    }

                    if(!myOutD3d.WglDxBuffer->isValid()
                     && myOutD3d.WglDxBuffer->resize(*myContext,
                                                     aSurf->TextureL(), aSurf->TextureLShare(),
                                                     aSurf->TextureR(), aSurf->TextureRShare(),
                                                     myOutD3d.DxWindow->getFboSizeX(),
                                                     myOutD3d.DxWindow->getFboSizeY())) {
                        myOutD3d.DxWindow->unlockLRBuffers();
                        break;
                    }

                    // unfortunately we need extra buffer to flip image - D3D and OpenGL use different coordinate rules
                    const GLfloat aDX = GLfloat(myOutD3d.GlBuffer->getVPSizeX()) / GLfloat(myOutD3d.GlBuffer->getSizeX());
                    const GLfloat aDY = GLfloat(myOutD3d.GlBuffer->getVPSizeY()) / GLfloat(myOutD3d.GlBuffer->getSizeY());
                    StArray<StGLVec2> aTCoords(4);
                    aTCoords[0] = StGLVec2(aDX,  aDY);
                    aTCoords[1] = StGLVec2(aDX,  0.0f);
                    aTCoords[2] = StGLVec2(0.0f, aDY);
                    aTCoords[3] = StGLVec2(0.0f, 0.0f);
                    myOutD3d.TCrdBuf.init(*myContext, aTCoords);

                    myContext->stglResizeViewport(myOutD3d.WglDxBuffer->getSizeX(),
                                                  myOutD3d.WglDxBuffer->getSizeY());

                    // render left view
                    myOutD3d.GlBuffer->bindBuffer(*myContext);
                    StWindow::signals.onRedraw(ST_DRAW_LEFT);
                    stglDrawExtra(ST_DRAW_LEFT, StGLDeviceControl::OUT_STEREO);
                    myOutD3d.GlBuffer->unbindBuffer(*myContext);

                    myOutD3d.WglDxBuffer->bindBufferL(*myContext);
                    myOutD3d.GlBuffer->bindTexture(*myContext);
                    myOutD3d.Program->use(*myContext);
                        myOutD3d.VertBuf.bindVertexAttrib(*myContext, myOutD3d.Program->getVVertexLoc());
                        myOutD3d.TCrdBuf.bindVertexAttrib(*myContext, myOutD3d.Program->getVTexCoordLoc());
                        myContext->core20fwd->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                        myOutD3d.TCrdBuf.unBindVertexAttrib(*myContext, myOutD3d.Program->getVTexCoordLoc());
                        myOutD3d.VertBuf.unBindVertexAttrib(*myContext, myOutD3d.Program->getVVertexLoc());
                    myOutD3d.Program->unuse(*myContext);
                    myOutD3d.GlBuffer->unbindTexture(*myContext);
                    myOutD3d.WglDxBuffer->unbindBufferL(*myContext);

                    // render right view
                    myOutD3d.GlBuffer->bindBuffer(*myContext);
                    StWindow::signals.onRedraw(ST_DRAW_RIGHT);
                    stglDrawExtra(ST_DRAW_RIGHT, StGLDeviceControl::OUT_STEREO);
                    myOutD3d.GlBuffer->unbindBuffer(*myContext);

                    myOutD3d.WglDxBuffer->bindBufferR(*myContext);
                    myOutD3d.GlBuffer->bindTexture(*myContext);
                    myOutD3d.Program->use(*myContext);
                        myOutD3d.VertBuf.bindVertexAttrib(*myContext, myOutD3d.Program->getVVertexLoc());
                        myOutD3d.TCrdBuf.bindVertexAttrib(*myContext, myOutD3d.Program->getVTexCoordLoc());
                        myContext->core20fwd->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                        myOutD3d.TCrdBuf.unBindVertexAttrib(*myContext, myOutD3d.Program->getVTexCoordLoc());
                        myOutD3d.VertBuf.unBindVertexAttrib(*myContext, myOutD3d.Program->getVVertexLoc());
                    myOutD3d.Program->unuse(*myContext);
                    myOutD3d.GlBuffer->unbindTexture(*myContext);
                    myOutD3d.WglDxBuffer->unbindBufferR(*myContext);

                    myOutD3d.DxWindow->unlockLRBuffers();
                    myContext->stglResizeViewport(aVPort);
                } else if(!myOutD3d.GlBuffer.isNull()) {
                    myOutD3d.GlBuffer->setupViewPort(*myContext); // we set TEXTURE sizes here
                    myOutD3d.GlBuffer->bindBuffer(*myContext);
                        StWindow::signals.onRedraw(ST_DRAW_LEFT);
                        stglDrawExtra(ST_DRAW_LEFT, StGLDeviceControl::OUT_STEREO);
                        while(myOutD3d.DxWindow->isInUpdate()) {
                            StThread::sleep(2);
                        }
                        // read OpenGL buffers and write into Direct3D surface
                        dxDraw(ST_DRAW_LEFT);
                        StWindow::signals.onRedraw(ST_DRAW_RIGHT);
                        stglDrawExtra(ST_DRAW_RIGHT, StGLDeviceControl::OUT_STEREO);
                        dxDraw(ST_DRAW_RIGHT);
                    myOutD3d.GlBuffer->unbindBuffer(*myContext);
                    myContext->stglResizeViewport(aVPort);
                }
                break;
            } else { //is windowed output - we doesn't break, just use aggressive behavior!
                // deactivate Direct3D Fullscreen window
                dxDisactivate();

                StWindow::stglMakeCurrent(ST_WIN_MASTER);
                StWindow::signals.onRedraw(ST_DRAW_RIGHT); // reverse order to avoid non-smooth mono->stereo transition
                StWindow::signals.onRedraw(ST_DRAW_LEFT);

                stglDrawWarning();

                myFPSControl.sleepToTarget();
                StWindow::stglSwap(ST_WIN_MASTER);
                ++myFPSControl;
                break;
            }
        }
    #endif
        default: {
            stglDrawAggressive(ST_DRAW_LEFT);
            stglDrawAggressive(ST_DRAW_RIGHT);
        }
    }
}

void StOutPageFlip::stglDrawExtra(unsigned int , int ) {
    //
}

void StOutPageFlip::stglDrawAggressive(unsigned int theView) {
    const StGLBoxPx aVPort = StWindow::stglViewport(ST_WIN_MASTER);
    StWindow::stglMakeCurrent(ST_WIN_MASTER);
    myContext->stglResizeViewport(aVPort);
    StWindow::signals.onRedraw(theView);

    if(myDevice == DEVICE_VUZIX) {
        if(!myVuzixSDK.isNull()) {
            if(theView == ST_DRAW_LEFT) { myVuzixSDK->waitAckLeft(); } else { myVuzixSDK->waitAckRight(); }
        }
    } else {
        stglDrawExtra(theView, StGLDeviceControl::OUT_STEREO);
    }

    StWindow::stglSwap(ST_WIN_MASTER);
    ++myFPSControl;

    // Inform VR920 to begin scanning on next vSync, a new right eye frame.
    if(myDevice == DEVICE_VUZIX && !myVuzixSDK.isNull()) {
        if(theView == ST_DRAW_LEFT) { myVuzixSDK->setLeft(); } else { myVuzixSDK->setRight(); }
    }
}

void StOutPageFlip::doSetQuadBuffer(const int32_t ) {
    myToResetDevice = true;
}

void StOutPageFlip::doShowExtra(const bool theValue) {
    myToResetDevice = true;
    if(theValue) {
        params.QuadBuffer->changeValues().add(myLangMap.changeValueId(STTR_PARAMETER_QB_EMULATED, "OpenGL Emulated"));
    } else {
        params.QuadBuffer->changeValues().remove(params.QuadBuffer->getValues().size() - 1);
        if(params.QuadBuffer->getValue() == QUADBUFFER_SOFT) {
            params.QuadBuffer->setValue(QUADBUFFER_HARD_OPENGL);
        }
    }
}

void StOutPageFlip::doSwitchVSync(const int32_t theValue) {
    if(myContext.isNull()) {
        return;
    }

    StWindow::stglMakeCurrent(ST_WIN_MASTER);
    if(params.QuadBuffer->getValue() == QUADBUFFER_SOFT) {
        //myContext->stglSetVSync(StGLContext::VSync_ON);
    } else {
        myContext->stglSetVSync((StGLContext::VSync_Mode )theValue);
    }
}
