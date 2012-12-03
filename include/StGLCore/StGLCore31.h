/**
 * Copyright © 2012 Kirill Gavrilov <kirill@sview.ru>
 *
 * Distributed under the Boost Software License, Version 1.0.
 * See accompanying file license-boost.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt
 */

#ifndef __StGLCore31_h_
#define __StGLCore31_h_

#include <StGLCore/StGLCore30.h>

/**
 * OpenGL 3.1 definition.
 */
template<typename theBaseClass_t>
struct ST_LOCAL stglTmplCore31 : public theBaseClass_t {

        public: //! @name GL_ARB_uniform_buffer_object (added to OpenGL 3.1 core)

    using theBaseClass_t::glGetUniformIndices;
    using theBaseClass_t::glGetActiveUniformsiv;
    using theBaseClass_t::glGetActiveUniformName;
    using theBaseClass_t::glGetUniformBlockIndex;
    using theBaseClass_t::glGetActiveUniformBlockiv;
    using theBaseClass_t::glGetActiveUniformBlockName;
    using theBaseClass_t::glUniformBlockBinding;

        public: //! @name GL_ARB_copy_buffer (added to OpenGL 3.1 core)

    using theBaseClass_t::glCopyBufferSubData;

        public: //! @name OpenGL 3.1 additives to 3.0

    using theBaseClass_t::glDrawArraysInstanced;
    using theBaseClass_t::glDrawElementsInstanced;
    using theBaseClass_t::glTexBuffer;
    using theBaseClass_t::glPrimitiveRestartIndex;

};

/**
 * OpenGL 3.1 compatibility profile.
 */
typedef stglTmplCore31<StGLCore30>    StGLCore31Back;

/**
 * OpenGL 3.1 core profile (without removed entry points marked as deprecated in 3.0).
 * Notice that GLSL versions 1.10 and 1.20 also removed in 3.1!
 */
typedef stglTmplCore31<StGLCore30Fwd> StGLCore31;

#endif // __StGLCore31_h_
