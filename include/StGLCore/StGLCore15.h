/**
 * Copyright © 2012 Kirill Gavrilov <kirill@sview.ru>
 *
 * Distributed under the Boost Software License, Version 1.0.
 * See accompanying file license-boost.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt
 */

#ifndef __StGLCore15_h_
#define __StGLCore15_h_

#include <StGLCore/StGLCore14.h>

/**
 * OpenGL 1.5 core based on 1.4 version.
 */
template<typename theBaseClass_t>
struct ST_LOCAL stglTmplCore15 : public theBaseClass_t {

        public: //! @name OpenGL 1.5 additives to 1.4

    using theBaseClass_t::glGenQueries;
    using theBaseClass_t::glDeleteQueries;
    using theBaseClass_t::glIsQuery;
    using theBaseClass_t::glBeginQuery;
    using theBaseClass_t::glEndQuery;
    using theBaseClass_t::glGetQueryiv;
    using theBaseClass_t::glGetQueryObjectiv;
    using theBaseClass_t::glGetQueryObjectuiv;
    using theBaseClass_t::glBindBuffer;
    using theBaseClass_t::glDeleteBuffers;
    using theBaseClass_t::glGenBuffers;
    using theBaseClass_t::glIsBuffer;
    using theBaseClass_t::glBufferData;
    using theBaseClass_t::glBufferSubData;
    using theBaseClass_t::glGetBufferSubData;
    using theBaseClass_t::glMapBuffer;
    using theBaseClass_t::glUnmapBuffer;
    using theBaseClass_t::glGetBufferParameteriv;
    using theBaseClass_t::glGetBufferPointerv;

};

/**
 * OpenGL 1.5 core based on 1.4 version.
 */
typedef stglTmplCore15<StGLCore14>    StGLCore15;

/**
 * OpenGL 1.5 without deprecated entry points.
 */
typedef stglTmplCore15<StGLCore14Fwd> StGLCore15Fwd;

#endif // __StGLCore15_h_
