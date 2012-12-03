/**
 * Copyright © 2011-2012 Kirill Gavrilov <kirill@sview.ru>
 *
 * Distributed under the Boost Software License, Version 1.0.
 * See accompanying file license-boost.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt
 */

#include <StGLMesh/StGLQuads.h>

#include <StGLCore/StGLCore11.h>

StGLQuads::StGLQuads()
: StGLMesh(GL_TRIANGLES) {
    //
}

StGLQuads::StGLQuads(const GLenum thePrimitives)
: StGLMesh(thePrimitives) {
    //
}

bool StGLQuads::initScreen(StGLContext& theCtx) {
    const GLfloat QUAD_VERTICES[4 * 4] = {
         1.0f,  1.0f, 0.0f, 1.0f, // top-right
         1.0f, -1.0f, 0.0f, 1.0f, // bottom-right
        -1.0f,  1.0f, 0.0f, 1.0f, // top-left
        -1.0f, -1.0f, 0.0f, 1.0f  // bottom-left
    };

    const GLfloat QUAD_TEXCOORD[2 * 4] = {
        1.0f, 0.0f, // top-right
        1.0f, 1.0f, // bottom-right
        0.0f, 0.0f, // top-left
        0.0f, 1.0f  // bottom-left
    };

    myPrimitives = GL_TRIANGLE_STRIP;
    return myVertexBuf.init(theCtx, 4, 4, QUAD_VERTICES)
        && myTCoordBuf.init(theCtx, 2, 4, QUAD_TEXCOORD);
}
