/**
 * Copyright © 2011 Kirill Gavrilov <kirill@sview.ru>
 *
 * Distributed under the Boost Software License, Version 1.0.
 * See accompanying file license-boost.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt
 */

#include <StGLWidgets/StGLRadioButtonTextured.h>

namespace {
    static const StString CLASS_NAME("StGLRadioButtonTextured");
};

StGLRadioButtonTextured::StGLRadioButtonTextured(StGLWidget* theParent,
                                                 const StHandle<StInt32Param>& theTrackedValue, const int32_t theValueOn,
                                                 const StString& theTexturePath,
                                                 const int theLeft, const int theTop,
                                                 const StGLCorner theCorner)
: StGLTextureButton(theParent, theLeft, theTop, theCorner, 1),
  myTrackValue(theTrackedValue),
  myValueOn(theValueOn) {
    //
    StGLTextureButton::setTexturePath(&theTexturePath, 1);
    StGLTextureButton::signals.onBtnClick.connect(this, &StGLRadioButtonTextured::doClick);
}

StGLRadioButtonTextured::~StGLRadioButtonTextured() {
    //
}

const StString& StGLRadioButtonTextured::getClassName() {
    return CLASS_NAME;
}

void StGLRadioButtonTextured::doClick(const size_t ) {
    myTrackValue->setValue(myValueOn);
}
