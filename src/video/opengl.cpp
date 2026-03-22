/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file opengl.cpp OpenGL video driver support. */

#include "../stdafx.h"

/* Define to disable buffer syncing. Will increase max fast forward FPS but produces artifacts. Mainly useful for performance testing. */
// #define NO_GL_BUFFER_SYNC
/* Define to allow software rendering backends. */
// #define GL_ALLOW_SOFTWARE_RENDERER

#if defined(_WIN32)
#	include <windows.h>
#endif

#define GL_GLEXT_PROTOTYPES
#if defined(__APPLE__)
#	define GL_SILENCE_DEPRECATION
#	include <OpenGL/gl3.h>
#else
#	include <GL/gl.h>
#endif
#include "../3rdparty/opengl/glext.h"

#include "opengl.h"
#include "video_driver.hpp"
#include "motion_vector.h"
#include "temporal_upscale.h"
#include "pp_screenshot.h"
#include "upscale_plugin.h"
#include "../benchmark.h"
#include "../core/geometry_func.hpp"
#include "../core/math_func.hpp"
#include "../gfx_func.h"
#include "../debug.h"
#include "../blitter/factory.hpp"
#include "../window_func.h"
#include "../timer/timer_game_calendar.h"
#include "../timer/timer_game_tick.h"
#include "../window_gui.h"
#include "../zoom_func.h"
#include "../core/string_consumer.hpp"

#include "../table/opengl_shader.h"
#include "../table/postprocess_shader.h"
#include "../table/sprites.h"


#include "../safeguards.h"


/* Define function pointers of all OpenGL functions that we load dynamically. */

#define GL(function) static decltype(&function) _ ## function

GL(glGetString);
GL(glGetIntegerv);
GL(glGetError);
GL(glDebugMessageControl);
GL(glDebugMessageCallback);

GL(glDisable);
GL(glEnable);
GL(glViewport);
GL(glClear);
GL(glClearColor);
GL(glBlendFunc);
GL(glDrawArrays);

GL(glTexImage1D);
GL(glTexImage2D);
GL(glTexParameteri);
GL(glTexSubImage1D);
GL(glTexSubImage2D);
GL(glBindTexture);
GL(glDeleteTextures);
GL(glGenTextures);
GL(glPixelStorei);

GL(glActiveTexture);

GL(glGenBuffers);
GL(glDeleteBuffers);
GL(glBindBuffer);
GL(glBufferData);
GL(glBufferSubData);
GL(glMapBuffer);
GL(glUnmapBuffer);
GL(glClearBufferSubData);

GL(glBufferStorage);
GL(glMapBufferRange);
GL(glClientWaitSync);
GL(glFenceSync);
GL(glDeleteSync);

GL(glGenVertexArrays);
GL(glDeleteVertexArrays);
GL(glBindVertexArray);

GL(glCreateProgram);
GL(glDeleteProgram);
GL(glLinkProgram);
GL(glUseProgram);
GL(glGetProgramiv);
GL(glGetProgramInfoLog);
GL(glCreateShader);
GL(glDeleteShader);
GL(glShaderSource);
GL(glCompileShader);
GL(glAttachShader);
GL(glGetShaderiv);
GL(glGetShaderInfoLog);
GL(glGetUniformLocation);
GL(glUniform1i);
GL(glUniform1f);
GL(glUniform2f);
GL(glUniform4f);

GL(glGetAttribLocation);
GL(glEnableVertexAttribArray);
GL(glDisableVertexAttribArray);
GL(glVertexAttribPointer);
GL(glBindFragDataLocation);

GL(glGenFramebuffers);
GL(glDeleteFramebuffers);
GL(glBindFramebuffer);
GL(glFramebufferTexture2D);
GL(glCheckFramebufferStatus);
GL(glDrawBuffers);
GL(glBlitFramebuffer);

/* GL 4.3 compute shader functions. */
GL(glDispatchCompute);
GL(glMemoryBarrier);
GL(glShaderStorageBlockBinding);
GL(glBindImageTexture);
GL(glBindBufferBase);
GL(glUniform2i);
GL(glCopyTexSubImage2D);
GL(glReadPixels);

GL(glGenQueries);
GL(glDeleteQueries);
GL(glBeginQuery);
GL(glEndQuery);
GL(glGetQueryObjectui64v);

#undef GL


/** A simple 2D vertex with just position and texture. */
struct Simple2DVertex {
	float x, y;
	float u, v;
};

/** Maximum number of cursor sprites to cache. */
static const int MAX_CACHED_CURSORS = 48;

/* static */ OpenGLBackend *OpenGLBackend::instance = nullptr;

GetOGLProcAddressProc GetOGLProcAddress;

static std::optional<std::string_view> GlGetString(GLenum name)
{
	auto str = reinterpret_cast<const char *>(_glGetString(name));
	if (str == nullptr) return {};
	return str;
}

/**
 * Find a substring in a string made of space delimited elements. The substring
 * has to match the complete element, partial matches don't count.
 * @param string List of space delimited elements.
 * @param substring Substring to find.
 * @return Whether the substring was found.
 */
bool HasStringInExtensionList(std::string_view string, std::string_view substring)
{
	StringConsumer consumer{string};
	while (consumer.AnyBytesLeft()) {
		if (substring == consumer.ReadUntil(" ", StringConsumer::SKIP_ALL_SEPARATORS)) return true;
	}

	return false;
}

/**
 * Check if an OpenGL extension is supported by the current context.
 * @param extension The extension string to test.
 * @return True if the extension is supported, false if not.
 */
static bool IsOpenGLExtensionSupported(std::string_view extension)
{
	static PFNGLGETSTRINGIPROC glGetStringi = nullptr;
	static bool glGetStringi_loaded = false;

	/* Starting with OpenGL 3.0 the preferred API to get the extensions
	 * has changed. Try to load the required function once. */
	if (!glGetStringi_loaded) {
		if (IsOpenGLVersionAtLeast(3, 0)) glGetStringi = (PFNGLGETSTRINGIPROC)GetOGLProcAddress("glGetStringi");
		glGetStringi_loaded = true;
	}

	if (glGetStringi != nullptr) {
		/* New style: Each supported extension can be queried and compared independently. */
		GLint num_exts;
		_glGetIntegerv(GL_NUM_EXTENSIONS, &num_exts);

		for (GLint i = 0; i < num_exts; i++) {
			const char *entry = reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, i));
			if (entry != nullptr && entry == extension) return true;
		}
	} else if (auto str = GlGetString(GL_EXTENSIONS); str.has_value()) {
		/* Old style: A single, space-delimited string for all extensions. */
		return HasStringInExtensionList(*str, extension);
	}

	return false;
}

static uint8_t _gl_major_ver = 0; ///< Major OpenGL version.
static uint8_t _gl_minor_ver = 0; ///< Minor OpenGL version.

/**
 * Check if the current OpenGL version is equal or higher than a given one.
 * @param major Minimal major version.
 * @param minor Minimal minor version.
 * @pre OpenGL was initialized.
 * @return True if the OpenGL version is equal or higher than the requested one.
 */
bool IsOpenGLVersionAtLeast(uint8_t major, uint8_t minor)
{
	return (_gl_major_ver > major) || (_gl_major_ver == major && _gl_minor_ver >= minor);
}

/**
 * Try loading an OpenGL function.
 * @tparam F Type of the function pointer.
 * @param f Reference where to store the function pointer in.
 * @param name Name of the function.
 * @return True if the function could be bound.
 */
template <typename F>
static bool BindGLProc(F &f, const char *name)
{
	f = reinterpret_cast<F>(GetOGLProcAddress(name));
	return f != nullptr;
}

/**
 * Bind basic information functions.
 * @return \c true iff all procs could be bound.
 */
static bool BindBasicInfoProcs()
{
	if (!BindGLProc(_glGetString, "glGetString")) return false;
	if (!BindGLProc(_glGetIntegerv, "glGetIntegerv")) return false;
	if (!BindGLProc(_glGetError, "glGetError")) return false;

	return true;
}

/**
 * Bind OpenGL 1.0 and 1.1 functions.
 * @return \c true iff all procs could be bound.
 */
static bool BindBasicOpenGLProcs()
{
	if (!BindGLProc(_glDisable, "glDisable")) return false;
	if (!BindGLProc(_glEnable, "glEnable")) return false;
	if (!BindGLProc(_glViewport, "glViewport")) return false;
	if (!BindGLProc(_glTexImage1D, "glTexImage1D")) return false;
	if (!BindGLProc(_glTexImage2D, "glTexImage2D")) return false;
	if (!BindGLProc(_glTexParameteri, "glTexParameteri")) return false;
	if (!BindGLProc(_glTexSubImage1D, "glTexSubImage1D")) return false;
	if (!BindGLProc(_glTexSubImage2D, "glTexSubImage2D")) return false;
	if (!BindGLProc(_glBindTexture, "glBindTexture")) return false;
	if (!BindGLProc(_glDeleteTextures, "glDeleteTextures")) return false;
	if (!BindGLProc(_glGenTextures, "glGenTextures")) return false;
	if (!BindGLProc(_glPixelStorei, "glPixelStorei")) return false;
	if (!BindGLProc(_glClear, "glClear")) return false;
	if (!BindGLProc(_glClearColor, "glClearColor")) return false;
	if (!BindGLProc(_glBlendFunc, "glBlendFunc")) return false;
	if (!BindGLProc(_glDrawArrays, "glDrawArrays")) return false;
	if (!BindGLProc(_glCopyTexSubImage2D, "glCopyTexSubImage2D")) return false;
	if (!BindGLProc(_glReadPixels, "glReadPixels")) return false;

	return true;
}

/**
 * Bind texture-related extension functions.
 * @return \c true iff all extension procs could be bound.
 */
static bool BindTextureExtensions()
{
	if (IsOpenGLVersionAtLeast(1, 3)) {
		if (!BindGLProc(_glActiveTexture, "glActiveTexture")) return false;
	} else {
		if (!BindGLProc(_glActiveTexture, "glActiveTextureARB")) return false;
	}

	return true;
}

/**
 * Bind vertex buffer object extension functions.
 * @return \c true iff all extension procs could be bound.
 */
static bool BindVBOExtension()
{
	if (IsOpenGLVersionAtLeast(1, 5)) {
		if (!BindGLProc(_glGenBuffers, "glGenBuffers")) return false;
		if (!BindGLProc(_glDeleteBuffers, "glDeleteBuffers")) return false;
		if (!BindGLProc(_glBindBuffer, "glBindBuffer")) return false;
		if (!BindGLProc(_glBufferData, "glBufferData")) return false;
		if (!BindGLProc(_glBufferSubData, "glBufferSubData")) return false;
		if (!BindGLProc(_glMapBuffer, "glMapBuffer")) return false;
		if (!BindGLProc(_glUnmapBuffer, "glUnmapBuffer")) return false;
	} else {
		if (!BindGLProc(_glGenBuffers, "glGenBuffersARB")) return false;
		if (!BindGLProc(_glDeleteBuffers, "glDeleteBuffersARB")) return false;
		if (!BindGLProc(_glBindBuffer, "glBindBufferARB")) return false;
		if (!BindGLProc(_glBufferData, "glBufferDataARB")) return false;
		if (!BindGLProc(_glBufferSubData, "glBufferSubDataARB")) return false;
		if (!BindGLProc(_glMapBuffer, "glMapBufferARB")) return false;
		if (!BindGLProc(_glUnmapBuffer, "glUnmapBufferARB")) return false;
	}

	if (IsOpenGLVersionAtLeast(4, 3) || IsOpenGLExtensionSupported("GL_ARB_clear_buffer_object")) {
		BindGLProc(_glClearBufferSubData, "glClearBufferSubData");
	} else {
		_glClearBufferSubData = nullptr;
	}

	return true;
}

/**
 * Bind vertex array object extension functions.
 * @return \c true iff all extension procs could be bound.
 */
static bool BindVBAExtension()
{
	/* The APPLE and ARB variants have different semantics (that don't matter for us).
	 *  Successfully getting pointers to one variant doesn't mean it is supported for
	 *  the current context. Always check the extension strings as well. */
	if (IsOpenGLVersionAtLeast(3, 0) || IsOpenGLExtensionSupported("GL_ARB_vertex_array_object")) {
		if (!BindGLProc(_glGenVertexArrays, "glGenVertexArrays")) return false;
		if (!BindGLProc(_glDeleteVertexArrays, "glDeleteVertexArrays")) return false;
		if (!BindGLProc(_glBindVertexArray, "glBindVertexArray")) return false;
	} else if (IsOpenGLExtensionSupported("GL_APPLE_vertex_array_object")) {
		if (!BindGLProc(_glGenVertexArrays, "glGenVertexArraysAPPLE")) return false;
		if (!BindGLProc(_glDeleteVertexArrays, "glDeleteVertexArraysAPPLE")) return false;
		if (!BindGLProc(_glBindVertexArray, "glBindVertexArrayAPPLE")) return false;
	}

	return true;
}

/**
 * Bind extension functions for shader support.
 * @return \c true iff all extension procs could be bound.
 */
static bool BindShaderExtensions()
{
	if (IsOpenGLVersionAtLeast(2, 0)) {
		if (!BindGLProc(_glCreateProgram, "glCreateProgram")) return false;
		if (!BindGLProc(_glDeleteProgram, "glDeleteProgram")) return false;
		if (!BindGLProc(_glLinkProgram, "glLinkProgram")) return false;
		if (!BindGLProc(_glUseProgram, "glUseProgram")) return false;
		if (!BindGLProc(_glGetProgramiv, "glGetProgramiv")) return false;
		if (!BindGLProc(_glGetProgramInfoLog, "glGetProgramInfoLog")) return false;
		if (!BindGLProc(_glCreateShader, "glCreateShader")) return false;
		if (!BindGLProc(_glDeleteShader, "glDeleteShader")) return false;
		if (!BindGLProc(_glShaderSource, "glShaderSource")) return false;
		if (!BindGLProc(_glCompileShader, "glCompileShader")) return false;
		if (!BindGLProc(_glAttachShader, "glAttachShader")) return false;
		if (!BindGLProc(_glGetShaderiv, "glGetShaderiv")) return false;
		if (!BindGLProc(_glGetShaderInfoLog, "glGetShaderInfoLog")) return false;
		if (!BindGLProc(_glGetUniformLocation, "glGetUniformLocation")) return false;
		if (!BindGLProc(_glUniform1i, "glUniform1i")) return false;
		if (!BindGLProc(_glUniform1f, "glUniform1f")) return false;
		if (!BindGLProc(_glUniform2f, "glUniform2f")) return false;
		if (!BindGLProc(_glUniform4f, "glUniform4f")) return false;

		if (!BindGLProc(_glGetAttribLocation, "glGetAttribLocation")) return false;
		if (!BindGLProc(_glEnableVertexAttribArray, "glEnableVertexAttribArray")) return false;
		if (!BindGLProc(_glDisableVertexAttribArray, "glDisableVertexAttribArray")) return false;
		if (!BindGLProc(_glVertexAttribPointer, "glVertexAttribPointer")) return false;
	} else {
		/* In the ARB extension programs and shaders are in the same object space. */
		if (!BindGLProc(_glCreateProgram, "glCreateProgramObjectARB")) return false;
		if (!BindGLProc(_glDeleteProgram, "glDeleteObjectARB")) return false;
		if (!BindGLProc(_glLinkProgram, "glLinkProgramARB")) return false;
		if (!BindGLProc(_glUseProgram, "glUseProgramObjectARB")) return false;
		if (!BindGLProc(_glGetProgramiv, "glGetObjectParameterivARB")) return false;
		if (!BindGLProc(_glGetProgramInfoLog, "glGetInfoLogARB")) return false;
		if (!BindGLProc(_glCreateShader, "glCreateShaderObjectARB")) return false;
		if (!BindGLProc(_glDeleteShader, "glDeleteObjectARB")) return false;
		if (!BindGLProc(_glShaderSource, "glShaderSourceARB")) return false;
		if (!BindGLProc(_glCompileShader, "glCompileShaderARB")) return false;
		if (!BindGLProc(_glAttachShader, "glAttachObjectARB")) return false;
		if (!BindGLProc(_glGetShaderiv, "glGetObjectParameterivARB")) return false;
		if (!BindGLProc(_glGetShaderInfoLog, "glGetInfoLogARB")) return false;
		if (!BindGLProc(_glGetUniformLocation, "glGetUniformLocationARB")) return false;
		if (!BindGLProc(_glUniform1i, "glUniform1iARB")) return false;
		if (!BindGLProc(_glUniform1f, "glUniform1fARB")) return false;
		if (!BindGLProc(_glUniform2f, "glUniform2fARB")) return false;
		if (!BindGLProc(_glUniform4f, "glUniform4fARB")) return false;

		if (!BindGLProc(_glGetAttribLocation, "glGetAttribLocationARB")) return false;
		if (!BindGLProc(_glEnableVertexAttribArray, "glEnableVertexAttribArrayARB")) return false;
		if (!BindGLProc(_glDisableVertexAttribArray, "glDisableVertexAttribArrayARB")) return false;
		if (!BindGLProc(_glVertexAttribPointer, "glVertexAttribPointerARB")) return false;
	}

	/* Bind functions only needed when using GLSL 1.50 shaders. */
	if (IsOpenGLVersionAtLeast(3, 0)) {
		BindGLProc(_glBindFragDataLocation, "glBindFragDataLocation");
	} else if (IsOpenGLExtensionSupported("GL_EXT_gpu_shader4")) {
		BindGLProc(_glBindFragDataLocation, "glBindFragDataLocationEXT");
	} else {
		_glBindFragDataLocation = nullptr;
	}

	return true;
}

/**
 * Bind extension functions for persistent buffer mapping.
 * @return \c true iff all extension procs could be bound.
 */
static bool BindPersistentBufferExtensions()
{
	/* Optional functions for persistent buffer mapping. */
	if (IsOpenGLVersionAtLeast(3, 0)) {
		if (!BindGLProc(_glMapBufferRange, "glMapBufferRange")) return false;
	}
	if (IsOpenGLVersionAtLeast(4, 4) || IsOpenGLExtensionSupported("GL_ARB_buffer_storage")) {
		if (!BindGLProc(_glBufferStorage, "glBufferStorage")) return false;
	}
#ifndef NO_GL_BUFFER_SYNC
	if (IsOpenGLVersionAtLeast(3, 2) || IsOpenGLExtensionSupported("GL_ARB_sync")) {
		if (!BindGLProc(_glClientWaitSync, "glClientWaitSync")) return false;
		if (!BindGLProc(_glFenceSync, "glFenceSync")) return false;
		if (!BindGLProc(_glDeleteSync, "glDeleteSync")) return false;
	}
#endif

	return true;
}

/**
 * Bind FBO extension functions for post-processing pipeline.
 * @return \c true iff all extension procs could be bound.
 */
/**
 * Bind GL 4.3 compute shader extension functions.
 * @return \c true iff all compute procs could be bound.
 */
static bool BindComputeExtensions()
{
	if (!IsOpenGLVersionAtLeast(4, 3)) return false;
	if (!BindGLProc(_glDispatchCompute, "glDispatchCompute")) return false;
	if (!BindGLProc(_glMemoryBarrier, "glMemoryBarrier")) return false;
	if (!BindGLProc(_glShaderStorageBlockBinding, "glShaderStorageBlockBinding")) return false;
	if (!BindGLProc(_glBindImageTexture, "glBindImageTexture")) return false;
	if (!BindGLProc(_glBindBufferBase, "glBindBufferBase")) return false;
	if (!BindGLProc(_glUniform2i, "glUniform2i")) return false;
	return true;
}

static bool BindFBOExtensions()
{
	if (IsOpenGLVersionAtLeast(3, 0) || IsOpenGLExtensionSupported("GL_ARB_framebuffer_object")) {
		if (!BindGLProc(_glGenFramebuffers, "glGenFramebuffers")) return false;
		if (!BindGLProc(_glDeleteFramebuffers, "glDeleteFramebuffers")) return false;
		if (!BindGLProc(_glBindFramebuffer, "glBindFramebuffer")) return false;
		if (!BindGLProc(_glFramebufferTexture2D, "glFramebufferTexture2D")) return false;
		if (!BindGLProc(_glCheckFramebufferStatus, "glCheckFramebufferStatus")) return false;
		if (!BindGLProc(_glDrawBuffers, "glDrawBuffers")) return false;
		if (!BindGLProc(_glBlitFramebuffer, "glBlitFramebuffer")) return false;
		return true;
	}
	return false;
}

/**
 * Bind GL timer query functions for GPU benchmarking.
 * Requires OpenGL 3.3 or GL_ARB_timer_query.
 * @return \c true iff all procs could be bound.
 */
static bool BindTimerQueryProcs()
{
	if (IsOpenGLVersionAtLeast(3, 3) || IsOpenGLExtensionSupported("GL_ARB_timer_query")) {
		if (!BindGLProc(_glGenQueries, "glGenQueries")) return false;
		if (!BindGLProc(_glDeleteQueries, "glDeleteQueries")) return false;
		if (!BindGLProc(_glBeginQuery, "glBeginQuery")) return false;
		if (!BindGLProc(_glEndQuery, "glEndQuery")) return false;
		if (!BindGLProc(_glGetQueryObjectui64v, "glGetQueryObjectui64v")) return false;
		return true;
	}
	return false;
}

/**
 * Callback to receive OpenGL debug messages.
 * @param type The type of message.
 * @param severity The severity of the issue.
 * @param message The message to convey to the end user.
 */
void APIENTRY DebugOutputCallback(GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar *message, const void *)
{
	/* Make severity human readable. */
	std::string_view severity_str;
	switch (severity) {
		case GL_DEBUG_SEVERITY_HIGH:   severity_str = "high"; break;
		case GL_DEBUG_SEVERITY_MEDIUM: severity_str = "medium"; break;
		case GL_DEBUG_SEVERITY_LOW:    severity_str = "low"; break;
	}

	/* Make type human readable.*/
	std::string_view type_str = "Other";
	switch (type) {
		case GL_DEBUG_TYPE_ERROR:               type_str = "Error"; break;
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: type_str = "Deprecated"; break;
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  type_str = "Undefined behaviour"; break;
		case GL_DEBUG_TYPE_PERFORMANCE:         type_str = "Performance"; break;
		case GL_DEBUG_TYPE_PORTABILITY:         type_str = "Portability"; break;
	}

	Debug(driver, 6, "OpenGL: {} ({}) - {}", type_str, severity_str, message);
}

/** Enable OpenGL debug messages if supported. */
void SetupDebugOutput()
{
#ifndef NO_DEBUG_MESSAGES
	if (_debug_driver_level < 6) return;

	if (IsOpenGLVersionAtLeast(4, 3)) {
		BindGLProc(_glDebugMessageControl, "glDebugMessageControl");
		BindGLProc(_glDebugMessageCallback, "glDebugMessageCallback");
	} else if (IsOpenGLExtensionSupported("GL_ARB_debug_output")) {
		BindGLProc(_glDebugMessageControl, "glDebugMessageControlARB");
		BindGLProc(_glDebugMessageCallback, "glDebugMessageCallbackARB");
	}

	if (_glDebugMessageControl != nullptr && _glDebugMessageCallback != nullptr) {
		/* Enable debug output. As synchronous debug output costs performance, we only enable it with a high debug level. */
		_glEnable(GL_DEBUG_OUTPUT);
		if (_debug_driver_level >= 8) _glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

		_glDebugMessageCallback(&DebugOutputCallback, nullptr);
		/* Enable all messages on highest debug level.*/
		_glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, _debug_driver_level >= 9 ? GL_TRUE : GL_FALSE);
		/* Get debug messages for errors and undefined/deprecated behaviour. */
		_glDebugMessageControl(GL_DONT_CARE, GL_DEBUG_TYPE_ERROR, GL_DONT_CARE, 0, nullptr, GL_TRUE);
		_glDebugMessageControl(GL_DONT_CARE, GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR, GL_DONT_CARE, 0, nullptr, GL_TRUE);
		_glDebugMessageControl(GL_DONT_CARE, GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR, GL_DONT_CARE, 0, nullptr, GL_TRUE);
	}
#endif
}

/**
 * Create and initialize the singleton back-end class.
 * @param get_proc Callback to get an OpenGL function from the OS driver.
 * @param screen_res Current display resolution.
 * @return std::nullopt on success, error message otherwise.
 */
/* static */ std::optional<std::string_view> OpenGLBackend::Create(GetOGLProcAddressProc get_proc, const Dimension &screen_res)
{
	if (OpenGLBackend::instance != nullptr) OpenGLBackend::Destroy();

	GetOGLProcAddress = get_proc;

	OpenGLBackend::instance = new OpenGLBackend();
	return OpenGLBackend::instance->Init(screen_res);
}

/**
 * Free resources and destroy singleton back-end class.
 */
/* static */ void OpenGLBackend::Destroy()
{
	delete OpenGLBackend::instance;
	OpenGLBackend::instance = nullptr;
}

/**
 * Construct OpenGL back-end class.
 */
OpenGLBackend::OpenGLBackend() : cursor_cache(MAX_CACHED_CURSORS)
{
}

/**
 * Free allocated resources.
 */
OpenGLBackend::~OpenGLBackend()
{
	this->DestroyPostProcessFBOs();
	this->DestroyMVResources();
	if (_glDeleteProgram != nullptr) {
		_glDeleteProgram(this->remap_program);
		_glDeleteProgram(this->vid_program);
		_glDeleteProgram(this->pal_program);
		_glDeleteProgram(this->sprite_program);
		if (this->pp_blit_program != 0) _glDeleteProgram(this->pp_blit_program);
		if (this->pp_cas_program != 0) _glDeleteProgram(this->pp_cas_program);
		if (this->pp_fsr_easu_program != 0) _glDeleteProgram(this->pp_fsr_easu_program);
		if (this->pp_fsr_rcas_program != 0) _glDeleteProgram(this->pp_fsr_rcas_program);
		if (this->pp_fxaa_program != 0) _glDeleteProgram(this->pp_fxaa_program);
		if (this->pp_color_program != 0) _glDeleteProgram(this->pp_color_program);
		if (this->pp_vignette_program != 0) _glDeleteProgram(this->pp_vignette_program);
		if (this->pp_tiltshift_h_program != 0) _glDeleteProgram(this->pp_tiltshift_h_program);
		if (this->pp_tiltshift_v_program != 0) _glDeleteProgram(this->pp_tiltshift_v_program);
		if (this->pp_night_program != 0) _glDeleteProgram(this->pp_night_program);
		if (this->pp_grain_program != 0) _glDeleteProgram(this->pp_grain_program);
		if (this->pp_bicubic_program != 0) _glDeleteProgram(this->pp_bicubic_program);
		if (this->pp_pixel_smooth_program != 0) _glDeleteProgram(this->pp_pixel_smooth_program);
		if (this->pp_crt_program != 0) _glDeleteProgram(this->pp_crt_program);
		if (this->pp_lighting_program != 0) _glDeleteProgram(this->pp_lighting_program);
		if (this->pp_bloom_threshold_program != 0) _glDeleteProgram(this->pp_bloom_threshold_program);
		if (this->pp_bloom_blur_h_program != 0) _glDeleteProgram(this->pp_bloom_blur_h_program);
		if (this->pp_bloom_blur_v_program != 0) _glDeleteProgram(this->pp_bloom_blur_v_program);
		if (this->pp_bloom_composite_program != 0) _glDeleteProgram(this->pp_bloom_composite_program);
		if (this->pp_weather_program != 0) _glDeleteProgram(this->pp_weather_program);
		if (this->pp_shadow_program != 0) _glDeleteProgram(this->pp_shadow_program);
		if (this->pp_water_reflect_program != 0) _glDeleteProgram(this->pp_water_reflect_program);
		if (this->pp_ssao_program != 0) _glDeleteProgram(this->pp_ssao_program);
		if (this->pp_terrain_smooth_program != 0) _glDeleteProgram(this->pp_terrain_smooth_program);
		if (this->pp_tree_sway_program != 0) _glDeleteProgram(this->pp_tree_sway_program);
		if (this->pp_sky_program != 0) _glDeleteProgram(this->pp_sky_program);
		if (this->pp_dof_program != 0) _glDeleteProgram(this->pp_dof_program);
		if (this->pp_temporal_program != 0) _glDeleteProgram(this->pp_temporal_program);
		if (this->pp_downsample_program != 0) _glDeleteProgram(this->pp_downsample_program);
	}
	if (_glDeleteVertexArrays != nullptr) _glDeleteVertexArrays(1, &this->vao_quad);
	if (_glDeleteBuffers != nullptr) {
		_glDeleteBuffers(1, &this->vbo_quad);
		_glDeleteBuffers(1, &this->vid_pbo);
		_glDeleteBuffers(1, &this->anim_pbo);
	}
	if (_glDeleteTextures != nullptr) {
		this->InternalClearCursorCache();
		OpenGLSprite::Destroy();

		_glDeleteTextures(1, &this->vid_texture);
		_glDeleteTextures(1, &this->anim_texture);
		_glDeleteTextures(1, &this->pal_texture);
	}
}

static std::tuple<uint8_t, uint8_t> DecodeVersion(std::string_view ver)
{
	StringConsumer consumer{ver};
	int major = consumer.ReadIntegerBase<uint8_t>(10);
	if (consumer.ReadIf(".")) return {major, consumer.ReadIntegerBase<uint8_t>(10)};
	return {major, 0};
}

/**
 * Check for the needed OpenGL functionality and allocate all resources.
 * @param screen_res Current display resolution.
 * @return Error string or std::nullopt if successful.
 */
std::optional<std::string_view> OpenGLBackend::Init(const Dimension &screen_res)
{
	if (!BindBasicInfoProcs()) return "OpenGL not supported";

	/* Always query the supported OpenGL version as the current context might have changed. */
	auto ver = GlGetString(GL_VERSION);
	auto vend = GlGetString(GL_VENDOR);
	auto renderer = GlGetString(GL_RENDERER);

	if (!ver.has_value() || !vend.has_value() || !renderer.has_value()) return "OpenGL not supported";

	Debug(driver, 1, "OpenGL driver: {} - {} ({})", *vend, *renderer, *ver);

#ifndef GL_ALLOW_SOFTWARE_RENDERER
	/* Don't use MESA software rendering backends as they are slower than
	 * just using a non-OpenGL video driver. */
	if (renderer->starts_with("llvmpipe") || renderer->starts_with("softpipe")) return "Software renderer detected, not using OpenGL";
#endif

	std::tie(_gl_major_ver, _gl_minor_ver) = DecodeVersion(*ver);

#ifdef _WIN32
	/* Old drivers on Windows (especially if made by Intel) seem to be
	 * unstable, so cull the oldest stuff here.  */
	if (!IsOpenGLVersionAtLeast(3, 2)) return "Need at least OpenGL version 3.2 on Windows";
#endif

	if (!BindBasicOpenGLProcs()) return "Failed to bind basic OpenGL functions.";

	SetupDebugOutput();

	/* OpenGL 1.3 is the absolute minimum. */
	if (!IsOpenGLVersionAtLeast(1, 3)) return "OpenGL version >= 1.3 required";
	/* Check for non-power-of-two texture support. */
	if (!IsOpenGLVersionAtLeast(2, 0) && !IsOpenGLExtensionSupported("GL_ARB_texture_non_power_of_two")) return "Non-power-of-two textures not supported";
	/* Check for single element texture formats. */
	if (!IsOpenGLVersionAtLeast(3, 0) && !IsOpenGLExtensionSupported("GL_ARB_texture_rg")) return "Single element texture formats not supported";
	if (!BindTextureExtensions()) return "Failed to bind texture extension functions";
	/* Check for vertex buffer objects. */
	if (!IsOpenGLVersionAtLeast(1, 5) && !IsOpenGLExtensionSupported("ARB_vertex_buffer_object")) return "Vertex buffer objects not supported";
	if (!BindVBOExtension()) return "Failed to bind VBO extension functions";
	/* Check for pixel buffer objects. */
	if (!IsOpenGLVersionAtLeast(2, 1) && !IsOpenGLExtensionSupported("GL_ARB_pixel_buffer_object")) return "Pixel buffer objects not supported";
	/* Check for vertex array objects. */
	if (!IsOpenGLVersionAtLeast(3, 0) && (!IsOpenGLExtensionSupported("GL_ARB_vertex_array_object") || !IsOpenGLExtensionSupported("GL_APPLE_vertex_array_object"))) return "Vertex array objects not supported";
	if (!BindVBAExtension()) return "Failed to bind VBA extension functions";
	/* Check for shader objects. */
	if (!IsOpenGLVersionAtLeast(2, 0) && (!IsOpenGLExtensionSupported("GL_ARB_shader_objects") || !IsOpenGLExtensionSupported("GL_ARB_fragment_shader") || !IsOpenGLExtensionSupported("GL_ARB_vertex_shader"))) return "No shader support";
	if (!BindShaderExtensions()) return "Failed to bind shader extension functions";
	if (IsOpenGLVersionAtLeast(3, 2) && _glBindFragDataLocation == nullptr) return "OpenGL claims to support version 3.2 but doesn't have glBindFragDataLocation";

	this->persistent_mapping_supported = IsOpenGLVersionAtLeast(3, 0) && (IsOpenGLVersionAtLeast(4, 4) || IsOpenGLExtensionSupported("GL_ARB_buffer_storage"));
#ifndef NO_GL_BUFFER_SYNC
	this->persistent_mapping_supported = this->persistent_mapping_supported && (IsOpenGLVersionAtLeast(3, 2) || IsOpenGLExtensionSupported("GL_ARB_sync"));
#endif

	if (this->persistent_mapping_supported && !BindPersistentBufferExtensions()) {
		Debug(driver, 1, "OpenGL claims to support persistent buffer mapping but doesn't export all functions, not using persistent mapping.");
		this->persistent_mapping_supported = false;
	}
	if (this->persistent_mapping_supported) Debug(driver, 3, "OpenGL: Using persistent buffer mapping");

	/* Check maximum texture size against screen resolution. */
	GLint max_tex_size = 0;
	_glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex_size);
	if (std::max(screen_res.width, screen_res.height) > (uint)max_tex_size) return "Max supported texture size is too small";

	/* Check available texture units. */
	GLint max_tex_units = 0;
	_glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &max_tex_units);
	if (max_tex_units < 4) return "Not enough simultaneous textures supported";

	Debug(driver, 2, "OpenGL shading language version: {}, texture units = {}", GlGetString(GL_SHADING_LANGUAGE_VERSION).value_or("Unknown version"), max_tex_units);

	if (!this->InitShaders()) return "Failed to initialize shaders";

	/* Setup video buffer texture. */
	_glGenTextures(1, &this->vid_texture);
	_glBindTexture(GL_TEXTURE_2D, this->vid_texture);
	_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	_glBindTexture(GL_TEXTURE_2D, 0);
	if (_glGetError() != GL_NO_ERROR) return "Can't generate video buffer texture";

	/* Setup video buffer texture. */
	_glGenTextures(1, &this->anim_texture);
	_glBindTexture(GL_TEXTURE_2D, this->anim_texture);
	_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	_glBindTexture(GL_TEXTURE_2D, 0);
	if (_glGetError() != GL_NO_ERROR) return "Can't generate animation buffer texture";

	/* Setup palette texture. */
	_glGenTextures(1, &this->pal_texture);
	_glBindTexture(GL_TEXTURE_1D, this->pal_texture);
	_glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	_glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	_glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAX_LEVEL, 0);
	_glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	_glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	_glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA8, 256, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, nullptr);
	_glBindTexture(GL_TEXTURE_1D, 0);
	if (_glGetError() != GL_NO_ERROR) return "Can't generate palette lookup texture";

	/* Bind uniforms in rendering shader program. */
	GLint tex_location = _glGetUniformLocation(this->vid_program, "colour_tex");
	GLint palette_location = _glGetUniformLocation(this->vid_program, "palette");
	GLint sprite_location = _glGetUniformLocation(this->vid_program, "sprite");
	GLint screen_location = _glGetUniformLocation(this->vid_program, "screen");
	_glUseProgram(this->vid_program);
	_glUniform1i(tex_location, 0);     // Texture unit 0.
	_glUniform1i(palette_location, 1); // Texture unit 1.
	/* Values that result in no transform. */
	_glUniform4f(sprite_location, 0.0f, 0.0f, 1.0f, 1.0f);
	_glUniform2f(screen_location, 1.0f, 1.0f);

	/* Bind uniforms in palette rendering shader program. */
	tex_location = _glGetUniformLocation(this->pal_program, "colour_tex");
	palette_location = _glGetUniformLocation(this->pal_program, "palette");
	sprite_location = _glGetUniformLocation(this->pal_program, "sprite");
	screen_location = _glGetUniformLocation(this->pal_program, "screen");
	_glUseProgram(this->pal_program);
	_glUniform1i(tex_location, 0);     // Texture unit 0.
	_glUniform1i(palette_location, 1); // Texture unit 1.
	_glUniform4f(sprite_location, 0.0f, 0.0f, 1.0f, 1.0f);
	_glUniform2f(screen_location, 1.0f, 1.0f);

	/* Bind uniforms in remap shader program. */
	tex_location = _glGetUniformLocation(this->remap_program, "colour_tex");
	palette_location = _glGetUniformLocation(this->remap_program, "palette");
	GLint remap_location = _glGetUniformLocation(this->remap_program, "remap_tex");
	this->remap_sprite_loc = _glGetUniformLocation(this->remap_program, "sprite");
	this->remap_screen_loc = _glGetUniformLocation(this->remap_program, "screen");
	this->remap_zoom_loc = _glGetUniformLocation(this->remap_program, "zoom");
	this->remap_rgb_loc = _glGetUniformLocation(this->remap_program, "rgb");
	_glUseProgram(this->remap_program);
	_glUniform1i(tex_location, 0);     // Texture unit 0.
	_glUniform1i(palette_location, 1); // Texture unit 1.
	_glUniform1i(remap_location, 2);   // Texture unit 2.

	/* Bind uniforms in sprite shader program. */
	tex_location = _glGetUniformLocation(this->sprite_program, "colour_tex");
	palette_location = _glGetUniformLocation(this->sprite_program, "palette");
	remap_location = _glGetUniformLocation(this->sprite_program, "remap_tex");
	GLint pal_location = _glGetUniformLocation(this->sprite_program, "pal");
	this->sprite_sprite_loc = _glGetUniformLocation(this->sprite_program, "sprite");
	this->sprite_screen_loc = _glGetUniformLocation(this->sprite_program, "screen");
	this->sprite_zoom_loc = _glGetUniformLocation(this->sprite_program, "zoom");
	this->sprite_rgb_loc = _glGetUniformLocation(this->sprite_program, "rgb");
	this->sprite_crash_loc = _glGetUniformLocation(this->sprite_program, "crash");
	_glUseProgram(this->sprite_program);
	_glUniform1i(tex_location, 0);     // Texture unit 0.
	_glUniform1i(palette_location, 1); // Texture unit 1.
	_glUniform1i(remap_location, 2);   // Texture unit 2.
	_glUniform1i(pal_location, 3);     // Texture unit 3.
	(void)_glGetError(); // Clear errors.

	/* Create pixel buffer object as video buffer storage. */
	_glGenBuffers(1, &this->vid_pbo);
	_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, this->vid_pbo);
	_glGenBuffers(1, &this->anim_pbo);
	_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, this->anim_pbo);
	if (_glGetError() != GL_NO_ERROR) return "Can't allocate pixel buffer for video buffer";

	/* Prime vertex buffer with a full-screen quad and store
	 * the corresponding state in a vertex array object. */
	static const Simple2DVertex vert_array[] = {
		/*  x     y    u    v */
		{  1.f, -1.f, 1.f, 1.f },
		{  1.f,  1.f, 1.f, 0.f },
		{ -1.f, -1.f, 0.f, 1.f },
		{ -1.f,  1.f, 0.f, 0.f },
	};

	/* Create VAO. */
	_glGenVertexArrays(1, &this->vao_quad);
	_glBindVertexArray(this->vao_quad);

	/* Create and fill VBO. */
	_glGenBuffers(1, &this->vbo_quad);
	_glBindBuffer(GL_ARRAY_BUFFER, this->vbo_quad);
	_glBufferData(GL_ARRAY_BUFFER, sizeof(vert_array), vert_array, GL_STATIC_DRAW);
	if (_glGetError() != GL_NO_ERROR) return "Can't generate VBO for fullscreen quad";

	/* Set vertex state. */
	GLint loc_position = _glGetAttribLocation(this->vid_program, "position");
	GLint colour_position = _glGetAttribLocation(this->vid_program, "colour_uv");
	_glEnableVertexAttribArray(loc_position);
	_glEnableVertexAttribArray(colour_position);
	_glVertexAttribPointer(loc_position, 2, GL_FLOAT, GL_FALSE, sizeof(Simple2DVertex), (GLvoid *)offsetof(Simple2DVertex, x));
	_glVertexAttribPointer(colour_position, 2, GL_FLOAT, GL_FALSE, sizeof(Simple2DVertex), (GLvoid *)offsetof(Simple2DVertex, u));
	_glBindVertexArray(0);

	/* Create resources for sprite rendering. */
	if (!OpenGLSprite::Create()) return "Failed to create sprite rendering resources";

	/* Initialize post-processing pipeline (optional -- gracefully disabled if not supported). */
	this->pp_fbo_supported = BindFBOExtensions();
	if (this->pp_fbo_supported) {
		if (!this->InitPostProcessShaders()) {
			Debug(driver, 0, "OpenGL: Failed to initialize post-processing shaders, disabling post-processing");
			this->pp_fbo_supported = false;
		} else {
			Debug(driver, 1, "OpenGL: Post-processing pipeline available");
		}
	} else {
		Debug(driver, 1, "OpenGL: FBO extensions not available, post-processing disabled");
	}

	/* Initialize motion vector compute shader (optional, GL 4.3+). */
	this->InitMVCompute();

	/* Bind timer query functions for GPU benchmarking (optional). */
	if (!BindTimerQueryProcs()) {
		Debug(driver, 1, "OpenGL: Timer query extensions not available, GPU benchmarking will report 0");
	}

	this->PrepareContext();
	(void)_glGetError(); // Clear errors.

	/* Register GL pixel reader for PP screenshots. */
	SetPPPixelReader([](int x, int y, int w, int h, void *data) {
		_glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
	});

	/* Try to discover and load an upscale plugin (DLSS, FSR).
	 * Look for plugin DLLs in the game directory. */
	auto TryLoadPlugin = [](const char *name) -> bool {
		auto *plugin = LoadUpscalePlugin(name);
		return plugin != nullptr;
	};
#ifdef _WIN32
	if (!TryLoadPlugin("dlss_plugin.dll")) TryLoadPlugin("fsr_plugin.dll");
#else
	if (!TryLoadPlugin("./libdlss_plugin.so")) TryLoadPlugin("./libfsr_plugin.so");
#endif

	return std::nullopt;
}

void OpenGLBackend::PrepareContext()
{
	_glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	_glDisable(GL_DEPTH_TEST);
	/* Enable alpha blending using the src alpha factor. */
	_glEnable(GL_BLEND);
	_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

std::string OpenGLBackend::GetDriverName()
{
	auto renderer = GlGetString(GL_RENDERER);
	auto version = GlGetString(GL_VERSION);
	/* Skipping GL_VENDOR as it tends to be "obvious" from the renderer and version data, and just makes the string pointlessly longer */
	return fmt::format("{}, {}", renderer.value_or("Unknown renderer"), version.value_or("Unknown version"));
}

/**
 * Check a shader for compilation errors and log them if necessary.
 * @param shader Shader to check.
 * @return True if the shader is valid.
 */
static bool VerifyShader(GLuint shader)
{
	static ReusableBuffer<char> log_buf;

	GLint result = GL_FALSE;
	_glGetShaderiv(shader, GL_COMPILE_STATUS, &result);

	/* Output log if there is one. */
	GLint log_len = 0;
	_glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
	if (log_len > 0) {
		_glGetShaderInfoLog(shader, log_len, nullptr, log_buf.Allocate(log_len));
		Debug(driver, result != GL_TRUE ? 0 : 2, "{}", log_buf.GetBuffer()); // Always print on failure.
	}

	return result == GL_TRUE;
}

/**
 * Check a program for link errors and log them if necessary.
 * @param program Program to check.
 * @return True if the program is valid.
 */
static bool VerifyProgram(GLuint program)
{
	static ReusableBuffer<char> log_buf;

	GLint result = GL_FALSE;
	_glGetProgramiv(program, GL_LINK_STATUS, &result);

	/* Output log if there is one. */
	GLint log_len = 0;
	_glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
	if (log_len > 0) {
		_glGetProgramInfoLog(program, log_len, nullptr, log_buf.Allocate(log_len));
		Debug(driver, result != GL_TRUE ? 0 : 2, "{}", log_buf.GetBuffer()); // Always print on failure.
	}

	return result == GL_TRUE;
}

/**
 * Create all needed shader programs.
 * @return True if successful, false otherwise.
 */
bool OpenGLBackend::InitShaders()
{
	auto ver = GlGetString(GL_SHADING_LANGUAGE_VERSION);
	if (!ver.has_value()) return false;

	auto [glsl_major, glsl_minor] = DecodeVersion(*ver);

	bool glsl_150 = (IsOpenGLVersionAtLeast(3, 2) || glsl_major > 1 || (glsl_major == 1 && glsl_minor >= 5)) && _glBindFragDataLocation != nullptr;

	/* Create vertex shader. */
	GLuint vert_shader = _glCreateShader(GL_VERTEX_SHADER);
	_glShaderSource(vert_shader, glsl_150 ? lengthof(_vertex_shader_sprite_150) : lengthof(_vertex_shader_sprite), glsl_150 ? _vertex_shader_sprite_150 : _vertex_shader_sprite, nullptr);
	_glCompileShader(vert_shader);
	if (!VerifyShader(vert_shader)) return false;

	/* Create fragment shader for plain RGBA. */
	GLuint frag_shader_rgb = _glCreateShader(GL_FRAGMENT_SHADER);
	_glShaderSource(frag_shader_rgb, glsl_150 ? lengthof(_frag_shader_direct_150) : lengthof(_frag_shader_direct), glsl_150 ? _frag_shader_direct_150 : _frag_shader_direct, nullptr);
	_glCompileShader(frag_shader_rgb);
	if (!VerifyShader(frag_shader_rgb)) return false;

	/* Create fragment shader for paletted only. */
	GLuint frag_shader_pal = _glCreateShader(GL_FRAGMENT_SHADER);
	_glShaderSource(frag_shader_pal, glsl_150 ? lengthof(_frag_shader_palette_150) : lengthof(_frag_shader_palette), glsl_150 ? _frag_shader_palette_150 : _frag_shader_palette, nullptr);
	_glCompileShader(frag_shader_pal);
	if (!VerifyShader(frag_shader_pal)) return false;

	/* Sprite remap fragment shader. */
	GLuint remap_shader = _glCreateShader(GL_FRAGMENT_SHADER);
	_glShaderSource(remap_shader, glsl_150 ? lengthof(_frag_shader_rgb_mask_blend_150) : lengthof(_frag_shader_rgb_mask_blend), glsl_150 ? _frag_shader_rgb_mask_blend_150 : _frag_shader_rgb_mask_blend, nullptr);
	_glCompileShader(remap_shader);
	if (!VerifyShader(remap_shader)) return false;

	/* Sprite fragment shader. */
	GLuint sprite_shader = _glCreateShader(GL_FRAGMENT_SHADER);
	_glShaderSource(sprite_shader, glsl_150 ? lengthof(_frag_shader_sprite_blend_150) : lengthof(_frag_shader_sprite_blend), glsl_150 ? _frag_shader_sprite_blend_150 : _frag_shader_sprite_blend, nullptr);
	_glCompileShader(sprite_shader);
	if (!VerifyShader(sprite_shader)) return false;

	/* Link shaders to program. */
	this->vid_program = _glCreateProgram();
	_glAttachShader(this->vid_program, vert_shader);
	_glAttachShader(this->vid_program, frag_shader_rgb);

	this->pal_program = _glCreateProgram();
	_glAttachShader(this->pal_program, vert_shader);
	_glAttachShader(this->pal_program, frag_shader_pal);

	this->remap_program = _glCreateProgram();
	_glAttachShader(this->remap_program, vert_shader);
	_glAttachShader(this->remap_program, remap_shader);

	this->sprite_program = _glCreateProgram();
	_glAttachShader(this->sprite_program, vert_shader);
	_glAttachShader(this->sprite_program, sprite_shader);

	if (glsl_150) {
		/* Bind fragment shader outputs. */
		_glBindFragDataLocation(this->vid_program, 0, "colour");
		_glBindFragDataLocation(this->pal_program, 0, "colour");
		_glBindFragDataLocation(this->remap_program, 0, "colour");
		_glBindFragDataLocation(this->sprite_program, 0, "colour");
	}

	_glLinkProgram(this->vid_program);
	if (!VerifyProgram(this->vid_program)) return false;

	_glLinkProgram(this->pal_program);
	if (!VerifyProgram(this->pal_program)) return false;

	_glLinkProgram(this->remap_program);
	if (!VerifyProgram(this->remap_program)) return false;

	_glLinkProgram(this->sprite_program);
	if (!VerifyProgram(this->sprite_program)) return false;

	_glDeleteShader(vert_shader);
	_glDeleteShader(frag_shader_rgb);
	_glDeleteShader(frag_shader_pal);
	_glDeleteShader(remap_shader);
	_glDeleteShader(sprite_shader);

	return true;
}

/**
 * Clear the bound pixel buffer to a specific value.
 * @param len Length of the buffer.
 * @param data Value to set.
 * @tparam T Pixel type.
 */
template <class T>
static void ClearPixelBuffer(size_t len, T data)
{
	T *buf = reinterpret_cast<T *>(_glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_READ_WRITE));
	for (size_t i = 0; i < len; i++) {
		*buf++ = data;
	}
	_glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
}

/**
 * Change the size of the drawing window and allocate matching resources.
 * @param w New width of the window.
 * @param h New height of the window.
 * @param force Recreate resources even if size didn't change.
 * @return \c false if nothing had to be done, \c true otherwise.
 */
bool OpenGLBackend::Resize(int w, int h, bool force)
{
	/* When PP scaling is active, _screen stores render resolution (not window size).
	 * Compare against display size to avoid redundant re-allocation every frame. */
	if (!force && _screen.width == w && _screen.height == h) return false;

	/* CPU blitter ALWAYS renders at display resolution. Render scaling is GPU-only
	 * (handled by FBO pipeline upscale/downsample shaders via pp_render_size).
	 * _screen must match window size for correct UI/viewport/mouse/dirty rects. */
	int render_w = w;
	int render_h = h;
	Blitter *cur_blitter = BlitterFactory::GetCurrentBlitter();

	int bpp = cur_blitter != nullptr ? cur_blitter->GetScreenDepth() : 32;
	int pitch = Align(render_w, 4);
	size_t line_pixel_count = static_cast<size_t>(pitch) * render_h;

	_glViewport(0, 0, w, h);

	_glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch);

	this->vid_buffer = nullptr;
	if (this->persistent_mapping_supported) {
		_glDeleteBuffers(1, &this->vid_pbo);
		_glGenBuffers(1, &this->vid_pbo);
		_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, this->vid_pbo);
		_glBufferStorage(GL_PIXEL_UNPACK_BUFFER, line_pixel_count * bpp / 8, nullptr, GL_MAP_READ_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT | GL_CLIENT_STORAGE_BIT);
	} else {
		/* Re-allocate video buffer texture and backing store. */
		_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, this->vid_pbo);
		_glBufferData(GL_PIXEL_UNPACK_BUFFER, line_pixel_count * bpp / 8, nullptr, GL_DYNAMIC_DRAW);
	}

	if (bpp == 32) {
		/* Initialize backing store alpha to opaque for 32bpp modes. */
		Colour black(0, 0, 0);
		if (_glClearBufferSubData != nullptr) {
			_glClearBufferSubData(GL_PIXEL_UNPACK_BUFFER, GL_RGBA8, 0, line_pixel_count * bpp / 8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, &black.data);
		} else {
			ClearPixelBuffer<uint32_t>(line_pixel_count, black.data);
		}
	} else if (bpp == 8) {
		if (_glClearBufferSubData != nullptr) {
			uint8_t b = 0;
			_glClearBufferSubData(GL_PIXEL_UNPACK_BUFFER, GL_R8, 0, line_pixel_count, GL_RED, GL_UNSIGNED_BYTE, &b);
		} else {
			ClearPixelBuffer<uint8_t>(line_pixel_count, 0);
		}
	}

	_glActiveTexture(GL_TEXTURE0);
	_glBindTexture(GL_TEXTURE_2D, this->vid_texture);
	if (bpp == 8) {
		_glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, render_w, render_h, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
	} else {
		_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, render_w, render_h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, nullptr);
	}
	_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

	/* Does this blitter need a separate animation buffer? */
	if (cur_blitter != nullptr && cur_blitter->NeedsAnimationBuffer()) {
		this->anim_buffer = nullptr;
		if (this->persistent_mapping_supported) {
			_glDeleteBuffers(1, &this->anim_pbo);
			_glGenBuffers(1, &this->anim_pbo);
			_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, this->anim_pbo);
			_glBufferStorage(GL_PIXEL_UNPACK_BUFFER, line_pixel_count, nullptr, GL_MAP_READ_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT | GL_CLIENT_STORAGE_BIT);
		} else {
			_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, this->anim_pbo);
			_glBufferData(GL_PIXEL_UNPACK_BUFFER, line_pixel_count, nullptr, GL_DYNAMIC_DRAW);
		}

		/* Initialize buffer as 0 == no remap. */
		if (_glClearBufferSubData != nullptr) {
			uint8_t b = 0;
			_glClearBufferSubData(GL_PIXEL_UNPACK_BUFFER, GL_R8, 0, line_pixel_count, GL_RED, GL_UNSIGNED_BYTE, &b);
		} else {
			ClearPixelBuffer<uint8_t>(line_pixel_count, 0);
		}

		_glBindTexture(GL_TEXTURE_2D, this->anim_texture);
		_glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, render_w, render_h, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
		_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	} else {
		if (this->anim_buffer != nullptr) {
			_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, this->anim_pbo);
			_glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
			_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
			this->anim_buffer = nullptr;
		}

		/* Allocate dummy texture that always reads as 0 == no remap. */
		uint dummy = 0;
		_glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		_glBindTexture(GL_TEXTURE_2D, this->anim_texture);
		_glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, &dummy);
	}

	_glBindTexture(GL_TEXTURE_2D, 0);

	/* Set screen dimensions to render resolution (this is what the CPU blitter sees). */
	_screen.height = render_h;
	_screen.width = render_w;
	_screen.pitch = pitch;
	_screen.dst_ptr = nullptr;

	/* Update screen size in remap shader program. */
	_glUseProgram(this->remap_program);
	_glUniform2f(this->remap_screen_loc, (float)_screen.width, (float)_screen.height);

	_glClear(GL_COLOR_BUFFER_BIT);

	/* Store display size and setup post-processing FBOs. */
	this->pp_display_size.width = w;
	this->pp_display_size.height = h;
	this->SetupPostProcessFBOs(w, h);

	return true;
}

/**
 * Update the stored palette.
 * @param pal Palette array with at least 256 elements.
 * @param first First entry to update.
 * @param length Number of entries to update.
 */
void OpenGLBackend::UpdatePalette(const Colour *pal, uint first, uint length)
{
	assert(first + length <= 256);

	_glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	_glActiveTexture(GL_TEXTURE1);
	_glBindTexture(GL_TEXTURE_1D, this->pal_texture);
	_glTexSubImage1D(GL_TEXTURE_1D, 0, first, length, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pal + first);
}

/**
 * Render video buffer to the screen.
 */
void OpenGLBackend::Paint()
{
	/* If a queued screenshot has settings to apply, do it BEFORE config sync
	 * so this frame renders with the correct PP state for the screenshot. */
	ApplyNextPPScreenshotSettings();

	/* Sync post-processing config from global settings. */
	if (this->pp_fbo_supported) {
		PostProcessConfig new_config;
		/* Bilinear filtering is independent of the PP toggle. */
		new_config.bilinear_filtering = (_video_texture_filter >= 1);
		new_config.bicubic_filtering = (_video_texture_filter == 2);

		if (_video_post_processing) {
			/* Core upscaling settings. */
			new_config.render_scale = _video_render_scale;
			uint8_t clamped_mode = Clamp<uint8_t>(_video_upscale_mode, 0, static_cast<uint8_t>(UpscaleMode::Plugin));
			new_config.upscale_mode = static_cast<UpscaleMode>(clamped_mode);
			new_config.sharpening = _video_sharpening;

			/* Effect toggles from persistent settings. */
			new_config.fxaa = _video_fxaa;
			new_config.fxaa_quality = _video_fxaa_quality;
			new_config.fxaa_threshold = _video_fxaa_threshold;
			new_config.night_mode = _video_night_mode;
			new_config.crt_filter = _video_crt_filter;
			new_config.vignette = _video_vignette;
			new_config.tiltshift = _video_tiltshift;
			new_config.film_grain = _video_film_grain;

			/* Color grading: enable the pass when any parameter deviates from identity.
			 * The shader is a visual no-op at identity values, so no flicker risk. */
			new_config.cg_brightness = _video_brightness;
			new_config.cg_contrast = _video_contrast;
			new_config.cg_saturation = _video_saturation;
			new_config.cg_temperature = _video_color_temperature;
			new_config.color_grading = (_video_brightness != 0 || _video_contrast != 100 ||
			                            _video_saturation != 100 || _video_color_temperature != 0);

			/* Night mode sub-parameters. */
			new_config.night_intensity = _video_night_intensity;
			new_config.night_blue_shift = _video_night_blue_shift;

			/* CRT sub-parameters. */
			new_config.crt_scanlines = _video_crt_scanlines;
			new_config.crt_curvature = _video_crt_curvature;
			new_config.crt_aberration = _video_crt_aberration;

			/* Vignette sub-parameters. */
			new_config.vignette_intensity = _video_vignette_intensity;
			new_config.vignette_radius = _video_vignette_radius;
			new_config.vignette_softness = _video_vignette_softness;

			/* Tilt-shift sub-parameters. */
			new_config.tiltshift_focus_y = _video_tiltshift_focus_y;
			new_config.tiltshift_focus_width = _video_tiltshift_focus_width;
			new_config.tiltshift_blur = _video_tiltshift_blur;

			/* Film grain sub-parameter. */
			new_config.grain_intensity = _video_grain_intensity;

			/* Dynamic lighting: compute time-of-day from game calendar.
			 * date_fract cycles 0..DAY_TICKS-1 within each game day. Map to 0.0..1.0. */
			new_config.dynamic_lighting = _video_dynamic_lighting;
			if (_video_dynamic_lighting) {
				new_config.time_of_day = static_cast<float>(TimerGameCalendar::date_fract) / static_cast<float>(Ticks::DAY_TICKS);
			}

			/* Bloom. */
			new_config.bloom = _video_bloom;
			new_config.bloom_threshold = _video_bloom_threshold;
			new_config.bloom_intensity = _video_bloom_intensity;

			/* Weather. */
			new_config.weather_type = _video_weather_type;
			new_config.weather_intensity = _video_weather_intensity;

			/* Pixel art smoothing. */
			new_config.pixel_smoothing = _video_pixel_smoothing;
			new_config.pixel_smooth_amount = _video_pixel_smooth_amount;

			/* Fake directional shadows. */
			new_config.fake_shadows = _video_fake_shadows;
			new_config.shadow_intensity = _video_shadow_intensity;
			new_config.shadow_angle = _video_shadow_angle;
			new_config.shadow_length = _video_shadow_length;
			new_config.shadow_softness = _video_shadow_softness;

			/* Water reflections. */
			new_config.water_reflections = _video_water_reflections;
			new_config.reflection_intensity = _video_reflection_intensity;
			new_config.reflection_distortion = _video_reflection_distortion;

			/* Screen-space ambient occlusion. */
			new_config.ssao = _video_ssao;
			new_config.ssao_radius = _video_ssao_radius;
			new_config.ssao_intensity = _video_ssao_intensity;
			new_config.ssao_samples = _video_ssao_samples;

			/* Terrain transition smoothing. */
			new_config.terrain_smooth = _video_terrain_smooth;
			new_config.terrain_smooth_radius = _video_terrain_smooth_radius;
			new_config.terrain_smooth_strength = _video_terrain_smooth_strength;

			/* Animated tree sway. */
			new_config.tree_sway = _video_tree_sway;
			new_config.tree_sway_amount = _video_tree_sway_amount;
			new_config.tree_sway_speed = _video_tree_sway_speed;

			/* Procedural sky with clouds. */
			new_config.sky_clouds = _video_sky_clouds;
			new_config.cloud_density = _video_cloud_density;
			new_config.cloud_speed = _video_cloud_speed;
			new_config.sky_brightness = _video_sky_brightness;

			/* Depth-of-field blur. */
			new_config.depth_of_field = _video_depth_of_field;
			new_config.dof_focus_point = _video_dof_focus_point;
			new_config.dof_aperture = _video_dof_aperture;
			new_config.dof_range = _video_dof_range;
		}

		new_config.auto_supersample = _video_auto_supersample;
		/* Auto-supersample at close zoom levels for smoother pixel art. */
		if (_video_auto_supersample && _video_post_processing) {
			const Window *mw = GetMainWindow();
			if (mw != nullptr && mw->viewport != nullptr) {
				ZoomLevel zoom = mw->viewport->zoom;
				if (zoom == ZoomLevel::In4x && new_config.render_scale < 200) {
					new_config.render_scale = 200; /* 2x SSAA at max zoom */
				} else if (zoom == ZoomLevel::In2x && new_config.render_scale < 150) {
					new_config.render_scale = 150; /* 1.5x SSAA at close zoom */
				}
			}
		}

		/* Rebuild FBOs when topology changes (effects on/off) or render_scale changes.
		 * Since render_scale no longer resizes the PBO (CPU blitter stays at display
		 * resolution), we just rebuild FBO textures at the new pp_render_size. No
		 * deferred mechanism needed — FBO rebuild is cheap (just texture realloc). */
		bool fbo_need_changed = PostProcessNeedsFBO(new_config) != PostProcessNeedsFBO(this->pp_config);
		bool scale_changed = (new_config.render_scale != this->pp_config.render_scale);
		bool config_changed = fbo_need_changed || scale_changed ||
		                      (new_config.upscale_mode != this->pp_config.upscale_mode);
		if (config_changed) this->pp_temporal_frame_count = 0;
		this->pp_config = new_config;
		if (fbo_need_changed || scale_changed) {
			this->SetupPostProcessFBOs(this->pp_display_size.width > 0 ? this->pp_display_size.width : _screen.width,
			                           this->pp_display_size.height > 0 ? this->pp_display_size.height : _screen.height);
		}
	}

	/* Activate motion vector recording when compute shader MV rasterization is available. */
	_motion_vectors.active = _video_post_processing && this->mv_compute_supported;

	/* Cache blitter properties to avoid repeated virtual calls per frame. */
	Blitter *blitter = BlitterFactory::GetCurrentBlitter();
	if (blitter == nullptr) return; /* No blitter available -- nothing to paint. */
	int bpp = blitter->GetScreenDepth();
	bool needs_anim = blitter->NeedsAnimationBuffer();

	/* Post-processing requires 32bpp blitter -- 8bpp palette indices are not valid RGBA input. */
	bool pp_this_frame = this->pp_active && bpp != 8;

	if (pp_this_frame) {
		/* Render scene into post-processing FBO at render resolution. */
		_glBindFramebuffer(GL_FRAMEBUFFER, this->pp_fbo[0]);
		_glViewport(0, 0, this->pp_render_size.width, this->pp_render_size.height);
	}

	_glClear(GL_COLOR_BUFFER_BIT);

	_glDisable(GL_BLEND);

	/* Blit video buffer to screen (or to FBO if post-processing is active). */
	_glActiveTexture(GL_TEXTURE0);
	_glBindTexture(GL_TEXTURE_2D, this->vid_texture);

	/* Apply bilinear filtering setting (only when not in 8bpp palette mode).
	 * Track filter state to avoid redundant GL calls and ensure NEAREST is
	 * restored when transitioning from LINEAR (e.g., PP disabled after use). */
	bool want_linear = (this->pp_config.bilinear_filtering || this->pp_config.upscale_mode == UpscaleMode::Bilinear) && bpp != 8;
	if (want_linear && !this->pp_vid_filter_was_linear) {
		_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		this->pp_vid_filter_was_linear = true;
	} else if (!want_linear && this->pp_vid_filter_was_linear) {
		_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		this->pp_vid_filter_was_linear = false;
	}

	_glActiveTexture(GL_TEXTURE1);
	_glBindTexture(GL_TEXTURE_1D, this->pal_texture);
	/* Is the blitter relying on a separate animation buffer? */
	if (needs_anim) {
		_glActiveTexture(GL_TEXTURE2);
		_glBindTexture(GL_TEXTURE_2D, this->anim_texture);
		_glUseProgram(this->remap_program);
		_glUniform4f(this->remap_sprite_loc, 0.0f, 0.0f, 1.0f, 1.0f);
		_glUniform2f(this->remap_screen_loc, 1.0f, 1.0f);
		_glUniform1f(this->remap_zoom_loc, 0);
		_glUniform1i(this->remap_rgb_loc, 1);
	} else {
		_glUseProgram(bpp == 8 ? this->pal_program : this->vid_program);
	}
	_glBindVertexArray(this->vao_quad);
	_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	/* Dispatch MV rasterization if active (generates MV + depth textures for FSR 2). */
	if (_motion_vectors.active && this->mv_compute_supported) {
		this->DispatchMVRasterization();
	}

	if (pp_this_frame) {
		/* Run post-processing chain. Final pass renders to default framebuffer (screen). */
		if (this->benchmark_query[0] != 0) this->BeginBenchmarkQuery();
		this->RenderPostProcess();
		if (this->benchmark_query_active) this->EndBenchmarkQuery();
	}

	_glEnable(GL_BLEND);

	/* Capture PP screenshot if one was requested (after all rendering is complete). */
	int capture_w = this->pp_active ? (int)this->pp_display_size.width : _screen.width;
	int capture_h = this->pp_active ? (int)this->pp_display_size.height : _screen.height;
	CapturePPScreenshotIfPending(capture_w, capture_h);
}

/**
 * Draw mouse cursor on screen.
 */
void OpenGLBackend::DrawMouseCursor()
{
	if (!this->cursor_in_window) return;

	/* Draw cursor on screen */
	_cur_dpi = &_screen;
	for (const auto &cs : this->cursor_sprites) {
		/* Sprites are cached by PopulateCursorCache(). */
		if (this->cursor_cache.Contains(cs.image.sprite)) {
			const OpenGLSprite *spr = this->cursor_cache.Get(cs.image.sprite).get();

			this->RenderOglSprite(spr, cs.image.pal,
					this->cursor_pos.x + cs.pos.x + UnScaleByZoom(spr->x_offs, _gui_zoom),
					this->cursor_pos.y + cs.pos.y + UnScaleByZoom(spr->y_offs, _gui_zoom),
					_gui_zoom);
		}
	}
}

/**
 * Initialize post-processing shader programs.
 * @return True if all shaders compiled and linked successfully.
 */
bool OpenGLBackend::InitPostProcessShaders()
{
	/* Post-processing requires GLSL 1.50 (OpenGL 3.2+). */
	if (!IsOpenGLVersionAtLeast(3, 2)) return false;

	GLuint pp_vert = _glCreateShader(GL_VERTEX_SHADER);
	_glShaderSource(pp_vert, lengthof(_vertex_shader_pp), _vertex_shader_pp, nullptr);
	_glCompileShader(pp_vert);
	if (!VerifyShader(pp_vert)) { _glDeleteShader(pp_vert); return false; }

	auto MakePPProgram = [&](const char **frag_src, int frag_count) -> GLuint {
		GLuint frag = _glCreateShader(GL_FRAGMENT_SHADER);
		_glShaderSource(frag, frag_count, frag_src, nullptr);
		_glCompileShader(frag);
		if (!VerifyShader(frag)) { _glDeleteShader(frag); return 0; }

		GLuint prog = _glCreateProgram();
		_glAttachShader(prog, pp_vert);
		_glAttachShader(prog, frag);
		if (_glBindFragDataLocation != nullptr) _glBindFragDataLocation(prog, 0, "frag_colour");
		_glLinkProgram(prog);
		_glDeleteShader(frag);

		if (!VerifyProgram(prog)) { _glDeleteProgram(prog); return 0; }
		return prog;
	};

	/* Compile all post-processing shader programs. Log failures individually. */
	auto CompileAndLog = [&](const char *name, const char **src, int count) -> GLuint {
		GLuint prog = MakePPProgram(src, count);
		if (prog == 0) Debug(driver, 0, "OpenGL: Post-processing shader '{}' failed to compile", name);
		return prog;
	};

	this->pp_blit_program = CompileAndLog("blit", _frag_shader_pp_blit, lengthof(_frag_shader_pp_blit));
	this->pp_cas_program = CompileAndLog("CAS", _frag_shader_pp_cas, lengthof(_frag_shader_pp_cas));
	this->pp_fsr_easu_program = CompileAndLog("upscale-EASU", _frag_shader_pp_fsr_easu, lengthof(_frag_shader_pp_fsr_easu));
	this->pp_fsr_rcas_program = CompileAndLog("sharpen-RCAS", _frag_shader_pp_fsr_rcas, lengthof(_frag_shader_pp_fsr_rcas));
	this->pp_fxaa_program = CompileAndLog("FXAA", _frag_shader_pp_fxaa, lengthof(_frag_shader_pp_fxaa));
	this->pp_color_program = CompileAndLog("color-grading", _frag_shader_pp_color_grading, lengthof(_frag_shader_pp_color_grading));
	this->pp_vignette_program = CompileAndLog("vignette", _frag_shader_pp_vignette, lengthof(_frag_shader_pp_vignette));
	this->pp_tiltshift_h_program = CompileAndLog("tiltshift-h", _frag_shader_pp_tiltshift_h, lengthof(_frag_shader_pp_tiltshift_h));
	this->pp_tiltshift_v_program = CompileAndLog("tiltshift-v", _frag_shader_pp_tiltshift_v, lengthof(_frag_shader_pp_tiltshift_v));
	this->pp_night_program = CompileAndLog("night-mode", _frag_shader_pp_night, lengthof(_frag_shader_pp_night));
	this->pp_grain_program = CompileAndLog("film-grain", _frag_shader_pp_grain, lengthof(_frag_shader_pp_grain));
	this->pp_bicubic_program = CompileAndLog("bicubic", _frag_shader_pp_bicubic, lengthof(_frag_shader_pp_bicubic));
	this->pp_pixel_smooth_program = CompileAndLog("pixel-smooth", _frag_shader_pp_pixel_smooth, lengthof(_frag_shader_pp_pixel_smooth));
	this->pp_crt_program = CompileAndLog("CRT", _frag_shader_pp_crt, lengthof(_frag_shader_pp_crt));
	this->pp_lighting_program = CompileAndLog("lighting", _frag_shader_pp_lighting, lengthof(_frag_shader_pp_lighting));
	this->pp_bloom_threshold_program = CompileAndLog("bloom-threshold", _frag_shader_pp_bloom_threshold, lengthof(_frag_shader_pp_bloom_threshold));
	this->pp_bloom_blur_h_program = CompileAndLog("bloom-blur-h", _frag_shader_pp_bloom_blur_h, lengthof(_frag_shader_pp_bloom_blur_h));
	this->pp_bloom_blur_v_program = CompileAndLog("bloom-blur-v", _frag_shader_pp_bloom_blur_v, lengthof(_frag_shader_pp_bloom_blur_v));
	this->pp_bloom_composite_program = CompileAndLog("bloom-composite", _frag_shader_pp_bloom_composite, lengthof(_frag_shader_pp_bloom_composite));
	this->pp_weather_program = CompileAndLog("weather", _frag_shader_pp_weather, lengthof(_frag_shader_pp_weather));
	this->pp_shadow_program = CompileAndLog("shadow", _frag_shader_pp_shadow, lengthof(_frag_shader_pp_shadow));
	this->pp_water_reflect_program = CompileAndLog("water-reflect", _frag_shader_pp_water_reflect, lengthof(_frag_shader_pp_water_reflect));
	this->pp_ssao_program = CompileAndLog("ssao", _frag_shader_pp_ssao, lengthof(_frag_shader_pp_ssao));
	this->pp_terrain_smooth_program = CompileAndLog("terrain-smooth", _frag_shader_pp_terrain_smooth, lengthof(_frag_shader_pp_terrain_smooth));
	this->pp_tree_sway_program = CompileAndLog("tree-sway", _frag_shader_pp_tree_sway, lengthof(_frag_shader_pp_tree_sway));
	this->pp_sky_program = CompileAndLog("sky", _frag_shader_pp_sky, lengthof(_frag_shader_pp_sky));
	this->pp_dof_program = CompileAndLog("depth-of-field", _frag_shader_pp_dof, lengthof(_frag_shader_pp_dof));
	this->pp_temporal_program = CompileAndLog("temporal-accum", _frag_shader_pp_temporal_accum, lengthof(_frag_shader_pp_temporal_accum));
	this->pp_downsample_program = CompileAndLog("downsample", _frag_shader_pp_downsample, lengthof(_frag_shader_pp_downsample));

	_glDeleteShader(pp_vert);

	/* Bind source_tex to texture unit 0 for all programs. */
	auto BindSourceTex = [&](GLuint prog) {
		if (prog == 0) return;
		_glUseProgram(prog);
		GLint loc = _glGetUniformLocation(prog, "source_tex");
		if (loc >= 0) _glUniform1i(loc, 0);
	};
	BindSourceTex(this->pp_blit_program);
	BindSourceTex(this->pp_cas_program);
	BindSourceTex(this->pp_fsr_easu_program);
	BindSourceTex(this->pp_fsr_rcas_program);
	BindSourceTex(this->pp_fxaa_program);
	BindSourceTex(this->pp_color_program);
	BindSourceTex(this->pp_vignette_program);
	BindSourceTex(this->pp_tiltshift_h_program);
	BindSourceTex(this->pp_tiltshift_v_program);
	BindSourceTex(this->pp_night_program);
	BindSourceTex(this->pp_grain_program);
	BindSourceTex(this->pp_bicubic_program);
	BindSourceTex(this->pp_pixel_smooth_program);
	BindSourceTex(this->pp_crt_program);
	BindSourceTex(this->pp_lighting_program);
	BindSourceTex(this->pp_bloom_threshold_program);
	BindSourceTex(this->pp_bloom_blur_h_program);
	BindSourceTex(this->pp_bloom_blur_v_program);
	BindSourceTex(this->pp_bloom_composite_program);
	/* Bloom composite also needs bloom_original on texture unit 1. */
	if (this->pp_bloom_composite_program != 0) {
		_glUseProgram(this->pp_bloom_composite_program);
		this->pp_bloom_composite_orig_loc = _glGetUniformLocation(this->pp_bloom_composite_program, "bloom_original");
		if (this->pp_bloom_composite_orig_loc >= 0) _glUniform1i(this->pp_bloom_composite_orig_loc, 1);
	}
	BindSourceTex(this->pp_weather_program);
	BindSourceTex(this->pp_shadow_program);
	BindSourceTex(this->pp_water_reflect_program);
	BindSourceTex(this->pp_ssao_program);
	BindSourceTex(this->pp_terrain_smooth_program);
	BindSourceTex(this->pp_tree_sway_program);
	BindSourceTex(this->pp_sky_program);
	BindSourceTex(this->pp_dof_program);
	BindSourceTex(this->pp_temporal_program);
	BindSourceTex(this->pp_downsample_program);
	/* Temporal shader uses additional texture units for history and MV. */
	if (this->pp_temporal_program != 0) {
		_glUseProgram(this->pp_temporal_program);
		this->pp_temporal_history_loc = _glGetUniformLocation(this->pp_temporal_program, "history_tex");
		this->pp_temporal_mv_loc = _glGetUniformLocation(this->pp_temporal_program, "mv_tex");
		if (this->pp_temporal_history_loc >= 0) _glUniform1i(this->pp_temporal_history_loc, 1);
		if (this->pp_temporal_mv_loc >= 0) _glUniform1i(this->pp_temporal_mv_loc, 2);
	}

	/* Cache ALL uniform locations at init time (not per-frame). */
	auto CacheLoc = [](GLuint prog, const char *name) -> GLint {
		return prog != 0 ? _glGetUniformLocation(prog, name) : -1;
	};

	/* CAS uniforms. */
	this->pp_cas_sharp_loc = CacheLoc(this->pp_cas_program, "sharpness");
	this->pp_cas_texel_loc = CacheLoc(this->pp_cas_program, "texel_size");

	/* FSR EASU uniforms. */
	this->pp_easu_con0_loc = CacheLoc(this->pp_fsr_easu_program, "easu_con0");
	this->pp_easu_con1_loc = CacheLoc(this->pp_fsr_easu_program, "easu_con1");
	this->pp_easu_con2_loc = CacheLoc(this->pp_fsr_easu_program, "easu_con2");
	this->pp_easu_con3_loc = CacheLoc(this->pp_fsr_easu_program, "easu_con3");

	/* FSR RCAS uniforms. */
	this->pp_rcas_con_loc = CacheLoc(this->pp_fsr_rcas_program, "rcas_strength");
	this->pp_rcas_texel_loc = CacheLoc(this->pp_fsr_rcas_program, "texel_size");

	/* FXAA uniforms. */
	this->pp_fxaa_texel_loc = CacheLoc(this->pp_fxaa_program, "texel_size");
	this->pp_fxaa_subpix_loc = CacheLoc(this->pp_fxaa_program, "subpix_quality");
	this->pp_fxaa_edge_loc = CacheLoc(this->pp_fxaa_program, "edge_threshold");

	/* Tilt-shift uniforms (H and V passes). */
	GLuint ts_progs[2] = {this->pp_tiltshift_h_program, this->pp_tiltshift_v_program};
	for (int i = 0; i < 2; i++) {
		this->pp_ts_texel_loc[i] = CacheLoc(ts_progs[i], "texel_size");
		this->pp_ts_focus_loc[i] = CacheLoc(ts_progs[i], "focus_y");
		this->pp_ts_width_loc[i] = CacheLoc(ts_progs[i], "focus_spread");
		this->pp_ts_blur_loc[i] = CacheLoc(ts_progs[i], "blur_strength");
	}

	/* Color grading uniforms. */
	this->pp_cg_brightness_loc = CacheLoc(this->pp_color_program, "brightness");
	this->pp_cg_contrast_loc = CacheLoc(this->pp_color_program, "contrast");
	this->pp_cg_saturation_loc = CacheLoc(this->pp_color_program, "saturation");
	this->pp_cg_temperature_loc = CacheLoc(this->pp_color_program, "temperature");

	/* Vignette uniforms. */
	this->pp_vig_intensity_loc = CacheLoc(this->pp_vignette_program, "vignette_strength");
	this->pp_vig_radius_loc = CacheLoc(this->pp_vignette_program, "vignette_radius");
	this->pp_vig_softness_loc = CacheLoc(this->pp_vignette_program, "vignette_softness");

	/* Night mode uniforms. */
	this->pp_night_int_loc = CacheLoc(this->pp_night_program, "night_amount");
	this->pp_night_blue_loc = CacheLoc(this->pp_night_program, "night_blue_shift");

	/* Film grain uniforms. */
	this->pp_grain_int_loc = CacheLoc(this->pp_grain_program, "grain_strength");
	this->pp_grain_time_loc = CacheLoc(this->pp_grain_program, "time");

	/* Bicubic uniforms. */
	this->pp_bicubic_texel_loc = CacheLoc(this->pp_bicubic_program, "texel_size");

	/* Pixel art smoothing uniforms. */
	this->pp_pixel_smooth_texel_loc = CacheLoc(this->pp_pixel_smooth_program, "texel_size");
	this->pp_pixel_smooth_amount_loc = CacheLoc(this->pp_pixel_smooth_program, "smooth_amount");

	/* CRT uniforms. */
	this->pp_crt_texel_loc = CacheLoc(this->pp_crt_program, "texel_size");
	this->pp_crt_res_loc = CacheLoc(this->pp_crt_program, "screen_size");
	this->pp_crt_scanline_loc = CacheLoc(this->pp_crt_program, "scanline_strength");
	this->pp_crt_curve_loc = CacheLoc(this->pp_crt_program, "curvature");
	this->pp_crt_aberr_loc = CacheLoc(this->pp_crt_program, "chromatic_aberr");

	/* Dynamic lighting uniforms. */
	this->pp_lighting_tod_loc = CacheLoc(this->pp_lighting_program, "time_of_day");

	/* Bloom uniforms. */
	this->pp_bloom_thresh_loc = CacheLoc(this->pp_bloom_threshold_program, "bloom_threshold");
	this->pp_bloom_int_loc = CacheLoc(this->pp_bloom_threshold_program, "bloom_intensity");
	this->pp_bloom_blur_h_texel_loc = CacheLoc(this->pp_bloom_blur_h_program, "texel_size");
	this->pp_bloom_blur_v_texel_loc = CacheLoc(this->pp_bloom_blur_v_program, "texel_size");

	/* Weather uniforms. */
	this->pp_weather_time_loc = CacheLoc(this->pp_weather_program, "time");
	this->pp_weather_int_loc = CacheLoc(this->pp_weather_program, "weather_intensity");
	this->pp_weather_type_loc = CacheLoc(this->pp_weather_program, "weather_type");

	/* Shadow uniforms. */
	this->pp_shadow_intensity_loc = CacheLoc(this->pp_shadow_program, "shadow_intensity");
	this->pp_shadow_dir_loc = CacheLoc(this->pp_shadow_program, "shadow_dir");
	this->pp_shadow_length_loc = CacheLoc(this->pp_shadow_program, "shadow_length");
	this->pp_shadow_samples_loc = CacheLoc(this->pp_shadow_program, "shadow_samples");
	this->pp_shadow_texel_loc = CacheLoc(this->pp_shadow_program, "texel_size");

	/* Water reflection uniforms. */
	this->pp_water_reflect_intensity_loc = CacheLoc(this->pp_water_reflect_program, "reflection_intensity");
	this->pp_water_reflect_distortion_loc = CacheLoc(this->pp_water_reflect_program, "distortion_amount");
	this->pp_water_reflect_time_loc = CacheLoc(this->pp_water_reflect_program, "time");
	this->pp_water_reflect_texel_loc = CacheLoc(this->pp_water_reflect_program, "texel_size");

	/* SSAO uniforms. */
	this->pp_ssao_radius_loc = CacheLoc(this->pp_ssao_program, "ssao_radius");
	this->pp_ssao_intensity_loc = CacheLoc(this->pp_ssao_program, "ssao_intensity");
	this->pp_ssao_samples_loc = CacheLoc(this->pp_ssao_program, "ssao_samples");
	this->pp_ssao_texel_loc = CacheLoc(this->pp_ssao_program, "texel_size");

	/* Terrain smoothing uniforms. */
	this->pp_terrain_smooth_radius_loc = CacheLoc(this->pp_terrain_smooth_program, "smooth_radius");
	this->pp_terrain_smooth_strength_loc = CacheLoc(this->pp_terrain_smooth_program, "smooth_strength");
	this->pp_terrain_smooth_texel_loc = CacheLoc(this->pp_terrain_smooth_program, "texel_size");

	/* Tree sway uniforms. */
	this->pp_tree_sway_amount_loc = CacheLoc(this->pp_tree_sway_program, "sway_amount");
	this->pp_tree_sway_speed_loc = CacheLoc(this->pp_tree_sway_program, "sway_speed");
	this->pp_tree_sway_time_loc = CacheLoc(this->pp_tree_sway_program, "time");
	this->pp_tree_sway_texel_loc = CacheLoc(this->pp_tree_sway_program, "texel_size");

	/* Sky/clouds uniforms. */
	this->pp_sky_density_loc = CacheLoc(this->pp_sky_program, "cloud_density");
	this->pp_sky_speed_loc = CacheLoc(this->pp_sky_program, "cloud_speed");
	this->pp_sky_brightness_loc = CacheLoc(this->pp_sky_program, "sky_brightness");
	this->pp_sky_time_loc = CacheLoc(this->pp_sky_program, "time");

	/* Depth-of-field uniforms. */
	this->pp_dof_focus_loc = CacheLoc(this->pp_dof_program, "focus_point");
	this->pp_dof_aperture_loc = CacheLoc(this->pp_dof_program, "aperture");
	this->pp_dof_range_loc = CacheLoc(this->pp_dof_program, "focus_range");
	this->pp_dof_texel_loc = CacheLoc(this->pp_dof_program, "texel_size");

	/* Downsample uniforms. */
	this->pp_downsample_texel_loc = CacheLoc(this->pp_downsample_program, "texel_size");

	/* Temporal accumulation uniforms. */
	this->pp_temporal_texel_loc = CacheLoc(this->pp_temporal_program, "texel_size");
	this->pp_temporal_jitter_loc = CacheLoc(this->pp_temporal_program, "jitter_offset");
	this->pp_temporal_reset_loc = CacheLoc(this->pp_temporal_program, "reset");

	(void)_glGetError(); /* Clear any errors from optional shader failures. */
	return this->pp_blit_program != 0; /* At minimum the blit program must work. */
}

/**
 * Setup post-processing FBOs at the given display resolution.
 * @param display_w Display width in pixels.
 * @param display_h Display height in pixels.
 * @return True if FBOs were set up successfully.
 */
bool OpenGLBackend::SetupPostProcessFBOs(int display_w, int display_h)
{
	this->DestroyPostProcessFBOs();

	if (!this->pp_fbo_supported || !PostProcessNeedsFBO(this->pp_config)) {
		this->pp_active = false;
		return true;
	}

	/* Guard against zero dimensions (minimized window). */
	if (display_w <= 0 || display_h <= 0) {
		this->pp_active = false;
		return true;
	}

	auto dims = CalculatePostProcessDimensions(display_w, display_h, this->pp_config.render_scale);
	this->pp_render_size = dims.render;
	this->pp_display_size = dims.display;

	_glGenFramebuffers(2, this->pp_fbo);
	_glGenTextures(2, this->pp_tex);

	/* When render_scale > 100 (supersampling), Paint() sets the viewport to
	 * render resolution, so both ping-pong textures must be large enough to
	 * hold those fragments. All effect passes run at render resolution;
	 * the final downsample pass writes to the backbuffer at display resolution.
	 * When render_scale <= 100, both FBOs are at display resolution as before. */
	Dimension fbo_size = (dims.render.width > dims.display.width) ? dims.render : dims.display;

	for (int i = 0; i < 2; i++) {
		_glBindTexture(GL_TEXTURE_2D, this->pp_tex[i]);
		_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
			fbo_size.width, fbo_size.height,
			0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		_glBindFramebuffer(GL_FRAMEBUFFER, this->pp_fbo[i]);
		_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, this->pp_tex[i], 0);

		GLenum draw_buf = GL_COLOR_ATTACHMENT0;
		_glDrawBuffers(1, &draw_buf);

		if (_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			Debug(driver, 0, "OpenGL: Post-processing FBO {} incomplete, disabling", i);
			this->DestroyPostProcessFBOs();
			_glBindFramebuffer(GL_FRAMEBUFFER, 0);
			return false;
		}
	}

	_glBindFramebuffer(GL_FRAMEBUFFER, 0);
	_glBindTexture(GL_TEXTURE_2D, 0);

	this->pp_active = true;
	Debug(driver, 1, "OpenGL: Post-processing FBOs created (render {}x{}, display {}x{})",
		dims.render.width, dims.render.height, dims.display.width, dims.display.height);
	return true;
}

/**
 * Destroy post-processing FBO resources.
 */
void OpenGLBackend::DestroyPostProcessFBOs()
{
	this->pp_active = false;
	if (this->pp_fbo[0] != 0) {
		_glDeleteFramebuffers(2, this->pp_fbo);
		this->pp_fbo[0] = this->pp_fbo[1] = 0;
	}
	if (this->pp_tex[0] != 0) {
		_glDeleteTextures(2, this->pp_tex);
		this->pp_tex[0] = this->pp_tex[1] = 0;
	}
	if (this->pp_history_fbo != 0) {
		_glDeleteFramebuffers(1, &this->pp_history_fbo);
		this->pp_history_fbo = 0;
	}
	if (this->pp_history_tex != 0) {
		_glDeleteTextures(1, &this->pp_history_tex);
		this->pp_history_tex = 0;
	}
}

/**
 * Execute the post-processing shader chain using ping-pong FBOs.
 * Called from Paint() after the scene has been rendered to pp_fbo[0].
 */
void OpenGLBackend::RenderPostProcess()
{
	/* Guard against zero-sized display (minimized window or failed resize). */
	if (this->pp_display_size.width == 0 || this->pp_display_size.height == 0) {
		_glBindFramebuffer(GL_FRAMEBUFFER, 0);
		return;
	}

	int src = 0;
	int pass = 0;
	int total = PostProcessPassCount(this->pp_config);

	if (total == 0) {
		/* No passes but FBO was bound -- just blit to screen. */
		_glBindFramebuffer(GL_FRAMEBUFFER, 0);
		_glViewport(0, 0, this->pp_display_size.width, this->pp_display_size.height);
		_glClear(GL_COLOR_BUFFER_BIT);
		_glActiveTexture(GL_TEXTURE0);
		_glBindTexture(GL_TEXTURE_2D, this->pp_tex[0]);
		if (this->pp_blit_program == 0) {
			/* Blit shader not available -- unbind FBO and return. */
			return;
		}
		_glUseProgram(this->pp_blit_program);
		_glBindVertexArray(this->vao_quad);
		_glDrawArrays(GL_TRIANGLES, 0, 3);
		return;
	}

	/* Count actual passes (only those with valid shader programs). */
	auto WillRun = [&](bool enabled, GLuint program) -> bool {
		return enabled && program != 0;
	};

	total = 0;
	if (this->pp_config.upscale_mode == UpscaleMode::FSR1) {
		if (this->pp_fsr_easu_program != 0) total++;
		if (this->pp_fsr_rcas_program != 0) total++;
	} else if (this->pp_config.upscale_mode == UpscaleMode::Bilinear && this->pp_config.render_scale < 100) {
		if (this->pp_config.bicubic_filtering && this->pp_bicubic_program != 0) {
			total++;
		} else if (this->pp_blit_program != 0) {
			total++;
		}
	}
	if (this->pp_config.upscale_mode == UpscaleMode::Temporal && this->pp_temporal_program != 0 && this->mv_compute_supported) total++;
	if (this->pp_config.upscale_mode == UpscaleMode::Plugin && GetLoadedUpscalePlugin() != nullptr) total++;
	if (this->pp_config.sharpening > 0 && this->pp_config.upscale_mode != UpscaleMode::FSR1 && this->pp_config.upscale_mode != UpscaleMode::Temporal && this->pp_config.upscale_mode != UpscaleMode::Plugin && this->pp_cas_program != 0) total++;
	if (WillRun(this->pp_config.sky_clouds, this->pp_sky_program)) total++;
	if (WillRun(this->pp_config.pixel_smoothing, this->pp_pixel_smooth_program)) total++;
	if (WillRun(this->pp_config.terrain_smooth, this->pp_terrain_smooth_program)) total++;
	if (WillRun(this->pp_config.tree_sway, this->pp_tree_sway_program)) total++;
	if (WillRun(this->pp_config.water_reflections, this->pp_water_reflect_program)) total++;
	if (WillRun(this->pp_config.ssao, this->pp_ssao_program)) total++;
	if (WillRun(this->pp_config.fxaa, this->pp_fxaa_program)) total++;
	if (WillRun(this->pp_config.tiltshift, this->pp_tiltshift_h_program)) total++;
	if (WillRun(this->pp_config.tiltshift, this->pp_tiltshift_v_program)) total++;
	if (WillRun(this->pp_config.color_grading, this->pp_color_program)) total++;
	if (WillRun(this->pp_config.night_mode, this->pp_night_program)) total++;
	if (WillRun(this->pp_config.vignette, this->pp_vignette_program)) total++;
	if (WillRun(this->pp_config.film_grain, this->pp_grain_program)) total++;
	if (WillRun(this->pp_config.dynamic_lighting, this->pp_lighting_program)) total++;
	if (this->pp_config.bloom && this->pp_bloom_threshold_program != 0) {
		total++; /* threshold */
		if (this->pp_bloom_blur_h_program != 0) total++; /* blur H */
		if (this->pp_bloom_blur_v_program != 0) total++; /* blur V */
		if (this->pp_bloom_composite_program != 0) total++; /* composite blend */
	}
	if (WillRun(this->pp_config.depth_of_field, this->pp_dof_program)) total++;
	if (WillRun(this->pp_config.fake_shadows, this->pp_shadow_program)) total++;
	if (WillRun(this->pp_config.crt_filter, this->pp_crt_program)) total++;
	if (this->pp_config.weather_type > 0 && this->pp_weather_program != 0) total++;
	if (this->pp_config.render_scale > 100 && this->pp_downsample_program != 0) total++;

	/* When supersampling (render_scale > 100) both ping-pong FBOs are at render
	 * resolution. Intermediate effect passes use the full render viewport; only
	 * the final pass (downsample or last effect) writes to the display-res backbuffer. */
	bool supersampling = (this->pp_render_size.width > this->pp_display_size.width);
	Dimension work_size = supersampling ? this->pp_render_size : this->pp_display_size;

	/* RunPass: program must already be bound via _glUseProgram before calling. */
	auto RunPass = [&]() {
		bool is_last = (pass == total - 1);

		if (is_last) {
			_glBindFramebuffer(GL_FRAMEBUFFER, 0);
			_glViewport(0, 0, this->pp_display_size.width, this->pp_display_size.height);
		} else {
			_glBindFramebuffer(GL_FRAMEBUFFER, this->pp_fbo[1 - src]);
			_glViewport(0, 0, work_size.width, work_size.height);
		}
		_glClear(GL_COLOR_BUFFER_BIT);

		_glActiveTexture(GL_TEXTURE0);
		_glBindTexture(GL_TEXTURE_2D, this->pp_tex[src]);

		/* PP vertex shader uses gl_VertexID fullscreen triangle (3 verts, not 4). */
		_glBindVertexArray(this->vao_quad);
		_glDrawArrays(GL_TRIANGLES, 0, 3);

		if (!is_last) src = 1 - src;
		pass++;
	};

	/* Effect texel size matches the working resolution of intermediate passes.
	 * When supersampling, effects run at render resolution for full quality;
	 * the downsample to display resolution happens as the final pass. */
	float texel_w = 1.0f / std::max(1u, work_size.width);
	float texel_h = 1.0f / std::max(1u, work_size.height);

	/* Upscaling passes. */
	if (this->pp_config.upscale_mode == UpscaleMode::FSR1 && this->pp_fsr_easu_program != 0) {
		float con0[4], con1[4], con2[4], con3[4];
		ComputeFsrEasuConstants(con0, con1, con2, con3,
			(float)this->pp_render_size.width, (float)this->pp_render_size.height,
			(float)this->pp_render_size.width, (float)this->pp_render_size.height,
			(float)this->pp_display_size.width, (float)this->pp_display_size.height);
		_glUseProgram(this->pp_fsr_easu_program);
		_glUniform4f(this->pp_easu_con0_loc, con0[0], con0[1], con0[2], con0[3]);
		_glUniform4f(this->pp_easu_con1_loc, con1[0], con1[1], con1[2], con1[3]);
		_glUniform4f(this->pp_easu_con2_loc, con2[0], con2[1], con2[2], con2[3]);
		_glUniform4f(this->pp_easu_con3_loc, con3[0], con3[1], con3[2], con3[3]);
		RunPass();

		if (this->pp_fsr_rcas_program != 0) {
			/* RCAS sharpening: use user value directly. 0 maps to 2.0 (no sharpening),
			 * which effectively makes this a passthrough. */
			_glUseProgram(this->pp_fsr_rcas_program);
			_glUniform2f(this->pp_rcas_texel_loc, texel_w, texel_h);
			_glUniform1f(this->pp_rcas_con_loc, MapSharpeningToFsrRcas(this->pp_config.sharpening));
			RunPass();
		}
	} else if (this->pp_config.upscale_mode == UpscaleMode::Bilinear && this->pp_config.render_scale < 100) {
		if (this->pp_config.bicubic_filtering && this->pp_bicubic_program != 0) {
			/* Bicubic (Catmull-Rom) upscale pass. texel_size is the input (render-resolution)
			 * texel pitch in display-texture UV space. */
			_glUseProgram(this->pp_bicubic_program);
			float render_texel_w = 1.0f / std::max(1u, this->pp_render_size.width);
			float render_texel_h = 1.0f / std::max(1u, this->pp_render_size.height);
			_glUniform2f(this->pp_bicubic_texel_loc, render_texel_w, render_texel_h);
			RunPass();
		} else if (this->pp_blit_program != 0) {
			_glUseProgram(this->pp_blit_program);
			RunPass();
		}
	}

	/* Temporal accumulation upscaling. */
	if (this->pp_config.upscale_mode == UpscaleMode::Temporal && this->pp_temporal_program != 0 && this->mv_compute_supported) {
		/* Allocate history texture and FBO if needed. */
		if (this->pp_history_tex == 0) {
			_glGenTextures(1, &this->pp_history_tex);
			_glBindTexture(GL_TEXTURE_2D, this->pp_history_tex);
			_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
				this->pp_display_size.width, this->pp_display_size.height,
				0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
			_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			_glBindTexture(GL_TEXTURE_2D, 0);

			/* Create FBO with history texture as color attachment for efficient blits. */
			_glGenFramebuffers(1, &this->pp_history_fbo);
			_glBindFramebuffer(GL_FRAMEBUFFER, this->pp_history_fbo);
			_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, this->pp_history_tex, 0);
			GLenum draw_buf = GL_COLOR_ATTACHMENT0;
			_glDrawBuffers(1, &draw_buf);
			if (_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
				Debug(driver, 0, "OpenGL: Temporal history FBO incomplete");
			}
			_glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}

		/* Compute jitter for this frame. */
		float jitter_x = 0.0f, jitter_y = 0.0f;
		const Window *main_window = GetMainWindow();
		int current_zoom = (main_window != nullptr && main_window->viewport != nullptr)
			? to_underlying(main_window->viewport->zoom)
			: to_underlying(ZoomLevel::Normal);
		if (ShouldApplyJitter(this->pp_config.render_scale, current_zoom)) {
			_jitter_sequence.NextFrame(jitter_x, jitter_y);
		}

		/* Detect scene cuts: large viewport jumps invalidate temporal history.
		 * Threshold is in virtual viewport units (~500 = half a screen at Normal zoom). */
		static constexpr int32_t SCENE_CUT_THRESHOLD = 500;
		bool scene_cut = false;
		if (this->pp_temporal_frame_count == 0) scene_cut = true; /* First frame after enable. */
		int32_t scroll_delta_x = _motion_vectors.prev_scroll_x - this->pp_temporal_prev_scroll_x;
		int32_t scroll_delta_y = _motion_vectors.prev_scroll_y - this->pp_temporal_prev_scroll_y;
		if (std::abs(scroll_delta_x) > SCENE_CUT_THRESHOLD || std::abs(scroll_delta_y) > SCENE_CUT_THRESHOLD) scene_cut = true;
		/* Store for next frame. */
		this->pp_temporal_prev_scroll_x = _motion_vectors.prev_scroll_x;
		this->pp_temporal_prev_scroll_y = _motion_vectors.prev_scroll_y;
		this->pp_temporal_frame_count++;

		_glUseProgram(this->pp_temporal_program);
		/* Temporal upscale samples from render-resolution input, not display-resolution. */
		float temporal_texel_w = 1.0f / std::max(1u, this->pp_render_size.width);
		float temporal_texel_h = 1.0f / std::max(1u, this->pp_render_size.height);
		_glUniform2f(this->pp_temporal_texel_loc, temporal_texel_w, temporal_texel_h);
		_glUniform2f(this->pp_temporal_jitter_loc, jitter_x, jitter_y);
		_glUniform1f(this->pp_temporal_reset_loc, scene_cut ? 1.0f : 0.0f);

		/* Bind history texture to texture unit 1 and MV texture to unit 2. */
		_glActiveTexture(GL_TEXTURE1);
		_glBindTexture(GL_TEXTURE_2D, this->pp_history_tex);
		_glActiveTexture(GL_TEXTURE2);
		_glBindTexture(GL_TEXTURE_2D, this->mv_texture);
		_glActiveTexture(GL_TEXTURE0);

		RunPass();

		/* Blit current output to history FBO for next frame.
		 * Uses glBlitFramebuffer for efficient GPU-to-GPU copy (no pipeline stall). */
		if (this->pp_history_fbo != 0) {
			/* The source is the current read framebuffer (pp_fbo[src] after RunPass swap). */
			_glBindFramebuffer(GL_READ_FRAMEBUFFER, pass > 0 ? this->pp_fbo[src] : this->pp_fbo[0]);
			_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, this->pp_history_fbo);
			_glBlitFramebuffer(0, 0, this->pp_display_size.width, this->pp_display_size.height,
			                   0, 0, this->pp_display_size.width, this->pp_display_size.height,
			                   GL_COLOR_BUFFER_BIT, GL_NEAREST);
			_glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}
	}

	/* External upscale plugin (DLSS, FSR 2/3). */
	if (this->pp_config.upscale_mode == UpscaleMode::Plugin) {
		UpscalePluginAPI *plugin = GetLoadedUpscalePlugin();
		if (plugin != nullptr && plugin->evaluate != nullptr) {
			float jitter_x = 0.0f, jitter_y = 0.0f;
			const Window *mw = GetMainWindow();
			int plugin_zoom = (mw != nullptr && mw->viewport != nullptr)
				? to_underlying(mw->viewport->zoom)
				: to_underlying(ZoomLevel::Normal);
			if (ShouldApplyJitter(this->pp_config.render_scale, plugin_zoom)) {
				_jitter_sequence.NextFrame(jitter_x, jitter_y);
			}

			UpscaleDispatchParams params = {};
			params.color_input.gl_texture_id = this->pp_tex[src];
			params.color_input.width = this->pp_render_size.width;
			params.color_input.height = this->pp_render_size.height;
			params.motion_vectors.gl_texture_id = this->mv_texture;
			params.motion_vectors.width = this->pp_render_size.width;
			params.motion_vectors.height = this->pp_render_size.height;
			params.depth.gl_texture_id = this->mv_depth_texture;
			params.depth.width = this->pp_render_size.width;
			params.depth.height = this->pp_render_size.height;
			params.output.gl_texture_id = this->pp_tex[1 - src];
			params.output.width = this->pp_display_size.width;
			params.output.height = this->pp_display_size.height;
			params.jitter_x = jitter_x;
			params.jitter_y = jitter_y;
			params.delta_time = 0.016f;
			params.render_width = this->pp_render_size.width;
			params.render_height = this->pp_render_size.height;
			params.display_width = this->pp_display_size.width;
			params.display_height = this->pp_display_size.height;

			plugin->evaluate(&params);
			src = 1 - src;
			pass++;
		} else {
			/* Plugin not available — do not execute any pass.
			 * The pass was not counted in total, so calling RunPass() here
			 * would desync the is_last detection. The scene in the FBO
			 * will be blitted by the safety fallback at the end if no
			 * other passes run. */
		}
	}

	/* CAS standalone (not when FSR1, Temporal, or Plugin active). */
	if (this->pp_config.sharpening > 0 && this->pp_config.upscale_mode != UpscaleMode::FSR1 && this->pp_config.upscale_mode != UpscaleMode::Temporal && this->pp_config.upscale_mode != UpscaleMode::Plugin && this->pp_cas_program != 0) {
		_glUseProgram(this->pp_cas_program);
		_glUniform2f(this->pp_cas_texel_loc, texel_w, texel_h);
		_glUniform1f(this->pp_cas_sharp_loc, MapSharpeningToCas(this->pp_config.sharpening));
		RunPass();
	}

	/* Procedural sky with clouds (background, runs first). */
	if (this->pp_config.sky_clouds && this->pp_sky_program != 0) {
		auto now = std::chrono::steady_clock::now();
		if (this->pp_weather_start_time == std::chrono::steady_clock::time_point{}) this->pp_weather_start_time = now;
		float sky_time = std::fmod(std::chrono::duration<float>(now - this->pp_weather_start_time).count(), 1000.0f);
		_glUseProgram(this->pp_sky_program);
		_glUniform1f(this->pp_sky_density_loc, this->pp_config.cloud_density / 100.0f);
		_glUniform1f(this->pp_sky_speed_loc, (float)this->pp_config.cloud_speed);
		_glUniform1f(this->pp_sky_brightness_loc, this->pp_config.sky_brightness / 100.0f);
		_glUniform1f(this->pp_sky_time_loc, sky_time);
		RunPass();
	}

	/* Pixel art smoothing (zoom-in enhancement). */
	if (this->pp_config.pixel_smoothing && this->pp_pixel_smooth_program != 0) {
		_glUseProgram(this->pp_pixel_smooth_program);
		_glUniform2f(this->pp_pixel_smooth_texel_loc, texel_w, texel_h);
		_glUniform1f(this->pp_pixel_smooth_amount_loc, this->pp_config.pixel_smooth_amount / 100.0f);
		RunPass();
	}

	/* Terrain transition smoothing (edge-aware bilateral filter). */
	if (this->pp_config.terrain_smooth && this->pp_terrain_smooth_program != 0) {
		_glUseProgram(this->pp_terrain_smooth_program);
		_glUniform2f(this->pp_terrain_smooth_texel_loc, texel_w, texel_h);
		_glUniform1f(this->pp_terrain_smooth_radius_loc, (float)this->pp_config.terrain_smooth_radius);
		_glUniform1f(this->pp_terrain_smooth_strength_loc, this->pp_config.terrain_smooth_strength / 100.0f);
		RunPass();
	}

	/* Animated tree/vegetation sway. */
	if (this->pp_config.tree_sway && this->pp_tree_sway_program != 0) {
		auto now = std::chrono::steady_clock::now();
		if (this->pp_weather_start_time == std::chrono::steady_clock::time_point{}) this->pp_weather_start_time = now;
		float sway_time = std::fmod(std::chrono::duration<float>(now - this->pp_weather_start_time).count(), 1000.0f);
		_glUseProgram(this->pp_tree_sway_program);
		_glUniform2f(this->pp_tree_sway_texel_loc, texel_w, texel_h);
		_glUniform1f(this->pp_tree_sway_amount_loc, (float)this->pp_config.tree_sway_amount);
		_glUniform1f(this->pp_tree_sway_speed_loc, (float)this->pp_config.tree_sway_speed);
		_glUniform1f(this->pp_tree_sway_time_loc, sway_time);
		RunPass();
	}

	/* Water reflections (screen-space).
	 * Use wall-clock time for wave animation so reflections animate
	 * even when dynamic lighting is off (time_of_day would be static). */
	if (this->pp_config.water_reflections && this->pp_water_reflect_program != 0) {
		auto now_reflect = std::chrono::steady_clock::now();
		if (this->pp_weather_start_time == std::chrono::steady_clock::time_point{}) this->pp_weather_start_time = now_reflect;
		float reflect_time = std::fmod(std::chrono::duration<float>(now_reflect - this->pp_weather_start_time).count(), 1000.0f);
		_glUseProgram(this->pp_water_reflect_program);
		_glUniform2f(this->pp_water_reflect_texel_loc, texel_w, texel_h);
		_glUniform1f(this->pp_water_reflect_intensity_loc, this->pp_config.reflection_intensity / 100.0f);
		_glUniform1f(this->pp_water_reflect_distortion_loc, (float)this->pp_config.reflection_distortion);
		_glUniform1f(this->pp_water_reflect_time_loc, reflect_time);
		RunPass();
	}

	/* Screen-space ambient occlusion. */
	if (this->pp_config.ssao && this->pp_ssao_program != 0) {
		_glUseProgram(this->pp_ssao_program);
		_glUniform2f(this->pp_ssao_texel_loc, texel_w, texel_h);
		_glUniform1f(this->pp_ssao_radius_loc, (float)this->pp_config.ssao_radius);
		_glUniform1f(this->pp_ssao_intensity_loc, this->pp_config.ssao_intensity / 100.0f);
		_glUniform1i(this->pp_ssao_samples_loc, Clamp((int)this->pp_config.ssao_samples, 4, 16));
		RunPass();
	}

	/* FXAA. */
	if (this->pp_config.fxaa && this->pp_fxaa_program != 0) {
		_glUseProgram(this->pp_fxaa_program);
		_glUniform2f(this->pp_fxaa_texel_loc, texel_w, texel_h);
		_glUniform1f(this->pp_fxaa_subpix_loc, this->pp_config.fxaa_quality / 100.0f);
		_glUniform1f(this->pp_fxaa_edge_loc, this->pp_config.fxaa_threshold / 100.0f);
		RunPass();
	}

	/* Tilt-shift (two passes: horizontal then vertical blur). */
	if (this->pp_config.tiltshift) {
		float focus_y = this->pp_config.tiltshift_focus_y / 100.0f;
		float focus_spread = this->pp_config.tiltshift_focus_width / 100.0f;
		float blur_s = this->pp_config.tiltshift_blur / 10.0f;
		for (int i = 0; i < 2; i++) {
			GLuint prog = (i == 0) ? this->pp_tiltshift_h_program : this->pp_tiltshift_v_program;
			if (prog == 0) continue;
			_glUseProgram(prog);
			_glUniform2f(this->pp_ts_texel_loc[i], texel_w, texel_h);
			_glUniform1f(this->pp_ts_focus_loc[i], focus_y);
			_glUniform1f(this->pp_ts_width_loc[i], focus_spread);
			_glUniform1f(this->pp_ts_blur_loc[i], blur_s);
			RunPass();
		}
	}

	/* Color grading. */
	if (this->pp_config.color_grading && this->pp_color_program != 0) {
		_glUseProgram(this->pp_color_program);
		_glUniform1f(this->pp_cg_brightness_loc, this->pp_config.cg_brightness / 100.0f);
		_glUniform1f(this->pp_cg_contrast_loc, this->pp_config.cg_contrast / 100.0f);
		_glUniform1f(this->pp_cg_saturation_loc, this->pp_config.cg_saturation / 100.0f);
		_glUniform1f(this->pp_cg_temperature_loc, this->pp_config.cg_temperature / 100.0f);
		RunPass();
	}

	/* Night mode. */
	if (this->pp_config.night_mode && this->pp_night_program != 0) {
		float ni = this->pp_config.night_intensity / 100.0f;
		float nb = this->pp_config.night_blue_shift / 100.0f;
		(void)_glGetError(); /* Clear pre-existing errors. */
		_glUseProgram(this->pp_night_program);
		GLenum e1 = _glGetError();
		_glUniform1f(this->pp_night_int_loc, ni);
		_glUniform1f(this->pp_night_blue_loc, nb);
		GLenum e2 = _glGetError();
		RunPass();
		GLenum e3 = _glGetError();
		Debug(driver, 0, "Night: ni={:.2f} nb={:.2f} errs=0x{:X}/0x{:X}/0x{:X} pass={}/{}",
			ni, nb, e1, e2, e3, pass, total);
	}

	/* Vignette. */
	if (this->pp_config.vignette && this->pp_vignette_program != 0) {
		_glUseProgram(this->pp_vignette_program);
		_glUniform1f(this->pp_vig_intensity_loc, this->pp_config.vignette_intensity / 100.0f);
		_glUniform1f(this->pp_vig_radius_loc, this->pp_config.vignette_radius / 100.0f);
		_glUniform1f(this->pp_vig_softness_loc, this->pp_config.vignette_softness / 100.0f);
		RunPass();
	}

	/* Film grain -- use wall-clock time wrapped to avoid float precision loss. */
	if (this->pp_config.film_grain && this->pp_grain_program != 0) {
		auto now = std::chrono::steady_clock::now();
		if (this->pp_grain_start_time == std::chrono::steady_clock::time_point{}) this->pp_grain_start_time = now;
		float elapsed = std::fmod(std::chrono::duration<float>(now - this->pp_grain_start_time).count(), 1000.0f);
		_glUseProgram(this->pp_grain_program);
		_glUniform1f(this->pp_grain_int_loc, this->pp_config.grain_intensity / 100.0f);
		_glUniform1f(this->pp_grain_time_loc, elapsed);
		RunPass();
	}

	/* Dynamic lighting (time-of-day). Runs after film grain, before bloom. */
	if (this->pp_config.dynamic_lighting && this->pp_lighting_program != 0) {
		_glUseProgram(this->pp_lighting_program);
		_glUniform1f(this->pp_lighting_tod_loc, this->pp_config.time_of_day);
		RunPass();
	}

	/* Bloom (4 passes: threshold -> blur H -> blur V -> composite).
	 * The composite pass additively blends the blurred glow with the pre-bloom scene.
	 * We save the pre-bloom source FBO so the composite can read the original. */
	if (this->pp_config.bloom && this->pp_bloom_threshold_program != 0) {
		/* Save the pre-bloom scene before the threshold pass destroys it.
		 * The 3 intermediate passes (threshold + blur_h + blur_v) overwrite both
		 * ping-pong FBOs, so the composite can't read the original from pp_tex.
		 * We use the history texture as scratch storage for the pre-bloom scene. */
		bool bloom_save_valid = false;
		/* Allocate history texture on demand if not already allocated by temporal mode.
		 * When supersampling, intermediate passes run at render resolution, so the
		 * history texture must match the working size to capture the full scene. */
		if (this->pp_bloom_composite_program != 0 && this->pp_history_tex == 0) {
			_glGenTextures(1, &this->pp_history_tex);
			_glBindTexture(GL_TEXTURE_2D, this->pp_history_tex);
			_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
				work_size.width, work_size.height,
				0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
			_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			_glBindTexture(GL_TEXTURE_2D, 0);
		}
		if (this->pp_bloom_composite_program != 0 && this->pp_history_tex != 0) {
			_glBindTexture(GL_TEXTURE_2D, this->pp_history_tex);
			_glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
				work_size.width, work_size.height);
			bloom_save_valid = true;
		}

		/* Pass 1: Extract bright pixels above threshold. */
		_glUseProgram(this->pp_bloom_threshold_program);
		_glUniform1f(this->pp_bloom_thresh_loc, this->pp_config.bloom_threshold / 100.0f);
		_glUniform1f(this->pp_bloom_int_loc, this->pp_config.bloom_intensity / 100.0f);
		RunPass();

		/* Pass 2: Horizontal blur. */
		if (this->pp_bloom_blur_h_program != 0) {
			_glUseProgram(this->pp_bloom_blur_h_program);
			_glUniform2f(this->pp_bloom_blur_h_texel_loc, texel_w, texel_h);
			RunPass();
		}

		/* Pass 3: Vertical blur. */
		if (this->pp_bloom_blur_v_program != 0) {
			_glUseProgram(this->pp_bloom_blur_v_program);
			_glUniform2f(this->pp_bloom_blur_v_texel_loc, texel_w, texel_h);
			RunPass();
		}

		/* Pass 4: Composite — blend blurred bloom with original scene.
		 * source_tex (unit 0) = blurred bloom result (current src FBO).
		 * bloom_original (unit 1) = saved pre-bloom scene in history texture. */
		if (this->pp_bloom_composite_program != 0 && bloom_save_valid) {
			_glUseProgram(this->pp_bloom_composite_program);
			_glActiveTexture(GL_TEXTURE1);
			_glBindTexture(GL_TEXTURE_2D, this->pp_history_tex);
			_glActiveTexture(GL_TEXTURE0);
			RunPass();
		}
	}

	/* Depth-of-field blur. */
	if (this->pp_config.depth_of_field && this->pp_dof_program != 0) {
		_glUseProgram(this->pp_dof_program);
		_glUniform2f(this->pp_dof_texel_loc, texel_w, texel_h);
		_glUniform1f(this->pp_dof_focus_loc, this->pp_config.dof_focus_point / 100.0f);
		_glUniform1f(this->pp_dof_aperture_loc, this->pp_config.dof_aperture / 100.0f);
		_glUniform1f(this->pp_dof_range_loc, this->pp_config.dof_range / 100.0f);
		RunPass();
	}

	/* Fake directional shadows. */
	if (this->pp_config.fake_shadows && this->pp_shadow_program != 0) {
		_glUseProgram(this->pp_shadow_program);
		_glUniform2f(this->pp_shadow_texel_loc, texel_w, texel_h);
		_glUniform1f(this->pp_shadow_intensity_loc, this->pp_config.shadow_intensity / 100.0f);
		float angle_rad = this->pp_config.shadow_angle * 3.14159265f / 180.0f;
		_glUniform2f(this->pp_shadow_dir_loc, std::cos(angle_rad), std::sin(angle_rad));
		_glUniform1f(this->pp_shadow_length_loc, (float)this->pp_config.shadow_length);
		_glUniform1i(this->pp_shadow_samples_loc, Clamp((int)this->pp_config.shadow_softness, 1, 10));
		RunPass();
	}

	/* CRT scanline filter. */
	if (this->pp_config.crt_filter && this->pp_crt_program != 0) {
		_glUseProgram(this->pp_crt_program);
		_glUniform2f(this->pp_crt_texel_loc, texel_w, texel_h);
		_glUniform2f(this->pp_crt_res_loc, (float)work_size.width, (float)work_size.height);
		_glUniform1f(this->pp_crt_scanline_loc, this->pp_config.crt_scanlines / 100.0f);
		_glUniform1f(this->pp_crt_curve_loc, this->pp_config.crt_curvature / 100.0f);
		_glUniform1f(this->pp_crt_aberr_loc, this->pp_config.crt_aberration / 10.0f);
		RunPass();
	}

	/* Weather overlay (rain/snow particles). Composites on top of everything. */
	if (this->pp_config.weather_type > 0 && this->pp_weather_program != 0) {
		auto now = std::chrono::steady_clock::now();
		if (this->pp_weather_start_time == std::chrono::steady_clock::time_point{}) this->pp_weather_start_time = now;
		float weather_time = std::fmod(std::chrono::duration<float>(now - this->pp_weather_start_time).count(), 1000.0f);
		_glUseProgram(this->pp_weather_program);
		_glUniform1f(this->pp_weather_time_loc, weather_time);
		_glUniform1f(this->pp_weather_int_loc, this->pp_config.weather_intensity / 100.0f);
		_glUniform1f(this->pp_weather_type_loc, static_cast<float>(this->pp_config.weather_type));
		RunPass();
	}

	/* Supersampling downsample. */
	if (this->pp_config.render_scale > 100 && this->pp_downsample_program != 0) {
		_glUseProgram(this->pp_downsample_program);
		float ss_texel_w = 1.0f / std::max(1u, this->pp_render_size.width);
		float ss_texel_h = 1.0f / std::max(1u, this->pp_render_size.height);
		_glUniform2f(this->pp_downsample_texel_loc, ss_texel_w, ss_texel_h);
		RunPass();
	}

	/* Safety: if no passes actually ran (e.g. all shader programs failed to compile),
	 * blit the FBO content to screen to avoid a black frame. */
	if (pass == 0) {
		_glBindFramebuffer(GL_FRAMEBUFFER, 0);
		_glViewport(0, 0, this->pp_display_size.width, this->pp_display_size.height);
		_glClear(GL_COLOR_BUFFER_BIT);
		if (this->pp_blit_program != 0) {
			_glActiveTexture(GL_TEXTURE0);
			_glBindTexture(GL_TEXTURE_2D, this->pp_tex[0]);
			_glUseProgram(this->pp_blit_program);
			_glBindVertexArray(this->vao_quad);
			_glDrawArrays(GL_TRIANGLES, 0, 3);
		}
	}

	/* Drain GL errors -- log at debug level to avoid per-frame spam. */
	for (GLenum err = _glGetError(); err != GL_NO_ERROR; err = _glGetError()) {
		Debug(driver, 6, "OpenGL: PP pipeline GL error: 0x{:04X} (pass {}/{})", err, pass, total);
	}
}

/**
 * Update the post-processing configuration.
 * Must be called from the draw thread (same thread as Paint()).
 * @param config New post-processing configuration.
 */
void OpenGLBackend::SetPostProcessConfig(const PostProcessConfig &config)
{
	bool needs_fbo_rebuild = (this->pp_config.render_scale != config.render_scale) ||
	                         (PostProcessNeedsFBO(this->pp_config) != PostProcessNeedsFBO(config));
	this->pp_config = config;
	if (needs_fbo_rebuild && this->pp_display_size.width > 0) {
		this->SetupPostProcessFBOs(this->pp_display_size.width, this->pp_display_size.height);
	}
}

/**
 * Initialize motion vector compute shader and resources (GL 4.3+).
 * @return True if initialization succeeded.
 */
bool OpenGLBackend::InitMVCompute()
{
	if (!BindComputeExtensions()) {
		Debug(driver, 1, "OpenGL: Compute shaders not available (GL < 4.3), MV rasterization disabled");
		return false;
	}

	GLuint shader = _glCreateShader(GL_COMPUTE_SHADER);
	_glShaderSource(shader, lengthof(_compute_shader_mv_rasterize), _compute_shader_mv_rasterize, nullptr);
	_glCompileShader(shader);
	if (!VerifyShader(shader)) {
		Debug(driver, 0, "OpenGL: MV compute shader failed to compile");
		_glDeleteShader(shader);
		return false;
	}

	this->mv_compute_program = _glCreateProgram();
	_glAttachShader(this->mv_compute_program, shader);
	_glLinkProgram(this->mv_compute_program);
	_glDeleteShader(shader);
	if (!VerifyProgram(this->mv_compute_program)) {
		Debug(driver, 0, "OpenGL: MV compute program failed to link");
		_glDeleteProgram(this->mv_compute_program);
		this->mv_compute_program = 0;
		return false;
	}

	this->mv_screen_size_loc = _glGetUniformLocation(this->mv_compute_program, "screen_size");
	this->mv_tile_count_loc = _glGetUniformLocation(this->mv_compute_program, "tile_count");
	this->mv_global_motion_loc = _glGetUniformLocation(this->mv_compute_program, "global_motion");
	this->mv_max_cmds_loc = _glGetUniformLocation(this->mv_compute_program, "max_cmds_per_tile");

	_glGenBuffers(1, &this->mv_cmd_ssbo);
	_glGenBuffers(1, &this->mv_tile_ssbo);

	_glGenTextures(1, &this->mv_texture);
	_glBindTexture(GL_TEXTURE_2D, this->mv_texture);
	_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

	_glGenTextures(1, &this->mv_depth_texture);
	_glBindTexture(GL_TEXTURE_2D, this->mv_depth_texture);
	_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	_glBindTexture(GL_TEXTURE_2D, 0);

	this->mv_compute_supported = true;
	Debug(driver, 1, "OpenGL: Motion vector compute shader initialized");
	return true;
}

/**
 * Free motion vector GPU resources.
 */
void OpenGLBackend::DestroyMVResources()
{
	this->mv_compute_supported = false;
	if (this->mv_compute_program != 0) { _glDeleteProgram(this->mv_compute_program); this->mv_compute_program = 0; }
	if (this->mv_cmd_ssbo != 0) { _glDeleteBuffers(1, &this->mv_cmd_ssbo); this->mv_cmd_ssbo = 0; }
	if (this->mv_tile_ssbo != 0) { _glDeleteBuffers(1, &this->mv_tile_ssbo); this->mv_tile_ssbo = 0; }
	if (this->mv_texture != 0) { _glDeleteTextures(1, &this->mv_texture); this->mv_texture = 0; }
	if (this->mv_depth_texture != 0) { _glDeleteTextures(1, &this->mv_depth_texture); this->mv_depth_texture = 0; }
}

/**
 * Dispatch the MV rasterization compute shader.
 * Uploads draw commands and tile bin data, dispatches one workgroup per 16x16 tile.
 */
void OpenGLBackend::DispatchMVRasterization()
{
	if (!this->mv_compute_supported || _motion_vectors.commands.empty()) return;

	int screen_w = this->pp_active ? (int)this->pp_render_size.width : _screen.width;
	int screen_h = this->pp_active ? (int)this->pp_render_size.height : _screen.height;
	if (screen_w <= 0 || screen_h <= 0) return;

	static TileBin tile_bin;
	tile_bin.Resize(screen_w, screen_h);
	tile_bin.Build(_motion_vectors.commands);

	_glBindTexture(GL_TEXTURE_2D, this->mv_texture);
	_glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, screen_w, screen_h, 0, GL_RG, GL_FLOAT, nullptr);
	_glBindTexture(GL_TEXTURE_2D, this->mv_depth_texture);
	_glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, screen_w, screen_h, 0, GL_RED, GL_FLOAT, nullptr);
	_glBindTexture(GL_TEXTURE_2D, 0);

	_glBindBuffer(GL_SHADER_STORAGE_BUFFER, this->mv_cmd_ssbo);
	_glBufferData(GL_SHADER_STORAGE_BUFFER, _motion_vectors.commands.size() * sizeof(DrawCommand),
		_motion_vectors.commands.data(), GL_DYNAMIC_DRAW);

	_glBindBuffer(GL_SHADER_STORAGE_BUFFER, this->mv_tile_ssbo);
	_glBufferData(GL_SHADER_STORAGE_BUFFER, tile_bin.data.size() * sizeof(int32_t),
		tile_bin.data.data(), GL_DYNAMIC_DRAW);
	_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, this->mv_cmd_ssbo);
	_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, this->mv_tile_ssbo);

	_glBindImageTexture(0, this->mv_texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
	_glBindImageTexture(1, this->mv_depth_texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16F);

	_glUseProgram(this->mv_compute_program);
	_glUniform2i(this->mv_screen_size_loc, screen_w, screen_h);
	_glUniform2i(this->mv_tile_count_loc, tile_bin.tiles_x, tile_bin.tiles_y);
	_glUniform2i(this->mv_global_motion_loc, _motion_vectors.scroll_dx, _motion_vectors.scroll_dy);
	_glUniform1i(this->mv_max_cmds_loc, TileBin::MAX_CMDS_PER_TILE);

	_glDispatchCompute((screen_w + 15) / 16, (screen_h + 15) / 16, 1);
	_glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

class OpenGLSpriteAllocator : public SpriteAllocator {
public:
	OpenGLSpriteLRUCache &lru;
	SpriteID sprite;

	OpenGLSpriteAllocator(OpenGLSpriteLRUCache &lru, SpriteID sprite) : lru(lru), sprite(sprite) {}
protected:
	void *AllocatePtr(size_t) override { NOT_REACHED(); }
};

void OpenGLBackend::PopulateCursorCache()
{
	if (this->clear_cursor_cache) {
		/* We have a pending cursor cache clear to do first. */
		this->clear_cursor_cache = false;
		this->last_sprite_pal = (PaletteID)-1;

		this->InternalClearCursorCache();
	}

	this->cursor_pos = _cursor.pos;
	this->cursor_in_window = _cursor.in_window;

	this->cursor_sprites.clear();
	for (const auto &sc : _cursor.sprites) {
		this->cursor_sprites.emplace_back(sc);

		if (!this->cursor_cache.Contains(sc.image.sprite)) {
			OpenGLSpriteAllocator allocator(this->cursor_cache, sc.image.sprite);
			GetRawSprite(sc.image.sprite, SpriteType::Normal, &allocator, this);
		}
	}
}

/**
 * Clear all cached cursor sprites.
 */
void OpenGLBackend::InternalClearCursorCache()
{
	this->cursor_cache.Clear();
}

/**
 * Queue a request for cursor cache clear.
 */
void OpenGLBackend::ClearCursorCache()
{
	/* If the game loop is threaded, this function might be called
	 * from the game thread. As we can call OpenGL functions only
	 * on the main thread, just set a flag that is handled the next
	 * time we prepare the cursor cache for drawing. */
	this->clear_cursor_cache = true;
}

/**
 * Get a pointer to the memory for the video driver to draw to.
 * @return Pointer to draw on.
 */
void *OpenGLBackend::GetVideoBuffer()
{
#ifndef NO_GL_BUFFER_SYNC
	if (this->sync_vid_mapping != nullptr) _glClientWaitSync(this->sync_vid_mapping, GL_SYNC_FLUSH_COMMANDS_BIT, 100000000); // 100ms timeout.
#endif

	if (!this->persistent_mapping_supported) {
		assert(this->vid_buffer == nullptr);
		_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, this->vid_pbo);
		this->vid_buffer = _glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_READ_WRITE);
	} else if (this->vid_buffer == nullptr) {
		_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, this->vid_pbo);
		Blitter *map_blitter = BlitterFactory::GetCurrentBlitter();
		int vid_bpp = map_blitter != nullptr ? map_blitter->GetScreenDepth() : 32;
		this->vid_buffer = _glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, static_cast<GLsizeiptr>(_screen.pitch) * _screen.height * vid_bpp / 8, GL_MAP_READ_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
	}

	return this->vid_buffer;
}

/**
 * Get a pointer to the memory for the separate animation buffer.
 * @return Pointer to draw on.
 */
uint8_t *OpenGLBackend::GetAnimBuffer()
{
	if (this->anim_pbo == 0) return nullptr;

#ifndef NO_GL_BUFFER_SYNC
	if (this->sync_anim_mapping != nullptr) _glClientWaitSync(this->sync_anim_mapping, GL_SYNC_FLUSH_COMMANDS_BIT, 100000000); // 100ms timeout.
#endif

	if (!this->persistent_mapping_supported) {
		_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, this->anim_pbo);
		this->anim_buffer = _glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_READ_WRITE);
	} else if (this->anim_buffer == nullptr) {
		_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, this->anim_pbo);
		this->anim_buffer = _glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, static_cast<GLsizeiptr>(_screen.pitch) * _screen.height, GL_MAP_READ_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
	}

	return (uint8_t *)this->anim_buffer;
}

/**
 * Update video buffer texture after the video buffer was filled.
 * @param update_rect Rectangle encompassing the dirty region of the video buffer.
 */
void OpenGLBackend::ReleaseVideoBuffer(const Rect &update_rect)
{
	assert(this->vid_pbo != 0);

	_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, this->vid_pbo);
	if (!this->persistent_mapping_supported) {
		_glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
		this->vid_buffer = nullptr;
	}

#ifndef NO_GL_BUFFER_SYNC
	if (this->persistent_mapping_supported) {
		_glDeleteSync(this->sync_vid_mapping);
		this->sync_vid_mapping = nullptr;
	}
#endif

	/* Update changed rect of the video buffer texture. */
	if (!IsEmptyRect(update_rect)) {
		_glActiveTexture(GL_TEXTURE0);
		_glBindTexture(GL_TEXTURE_2D, this->vid_texture);
		_glPixelStorei(GL_UNPACK_ROW_LENGTH, _screen.pitch);
		Blitter *release_blitter = BlitterFactory::GetCurrentBlitter();
		if (release_blitter != nullptr && release_blitter->GetScreenDepth() == 8) {
			_glTexSubImage2D(GL_TEXTURE_2D, 0, update_rect.left, update_rect.top, update_rect.right - update_rect.left, update_rect.bottom - update_rect.top, GL_RED, GL_UNSIGNED_BYTE, (GLvoid*)(size_t)(update_rect.top * _screen.pitch + update_rect.left));
		} else {
			_glTexSubImage2D(GL_TEXTURE_2D, 0, update_rect.left, update_rect.top, update_rect.right - update_rect.left, update_rect.bottom - update_rect.top, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, (GLvoid*)(size_t)(update_rect.top * _screen.pitch * 4 + update_rect.left * 4));
		}

#ifndef NO_GL_BUFFER_SYNC
		if (this->persistent_mapping_supported) this->sync_vid_mapping = _glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
#endif
	}
}

/**
 * Update animation buffer texture after the animation buffer was filled.
 * @param update_rect Rectangle encompassing the dirty region of the animation buffer.
 */
void OpenGLBackend::ReleaseAnimBuffer(const Rect &update_rect)
{
	if (this->anim_pbo == 0) return;

	_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, this->anim_pbo);
	if (!this->persistent_mapping_supported) {
		_glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
		this->anim_buffer = nullptr;
	}

#ifndef NO_GL_BUFFER_SYNC
	if (this->persistent_mapping_supported) {
		_glDeleteSync(this->sync_anim_mapping);
		this->sync_anim_mapping = nullptr;
	}
#endif

	/* Update changed rect of the video buffer texture. */
	if (update_rect.left != update_rect.right) {
		_glActiveTexture(GL_TEXTURE0);
		_glBindTexture(GL_TEXTURE_2D, this->anim_texture);
		_glPixelStorei(GL_UNPACK_ROW_LENGTH, _screen.pitch);
		_glTexSubImage2D(GL_TEXTURE_2D, 0, update_rect.left, update_rect.top, update_rect.right - update_rect.left, update_rect.bottom - update_rect.top, GL_RED, GL_UNSIGNED_BYTE, (GLvoid *)(size_t)(update_rect.top * _screen.pitch + update_rect.left));

#ifndef NO_GL_BUFFER_SYNC
		if (this->persistent_mapping_supported) this->sync_anim_mapping = _glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
#endif
	}
}

/* virtual */ Sprite *OpenGLBackend::Encode(SpriteType sprite_type, const SpriteLoader::SpriteCollection &sprite, SpriteAllocator &allocator)
{
	/* This encoding is only called for mouse cursors. We don't need real sprites but OpenGLSprites to show as cursor. These need to be put in the LRU cache. */
	OpenGLSpriteAllocator &gl_allocator = static_cast<OpenGLSpriteAllocator&>(allocator);
	gl_allocator.lru.Insert(gl_allocator.sprite, std::make_unique<OpenGLSprite>(sprite_type, sprite));

	return nullptr;
}

/**
 * Render a sprite to the back buffer.
 * @param gl_sprite Sprite to render.
 * @param pal The palette to draw the sprite with.
 * @param x X position of the sprite.
 * @param y Y position of the sprite.
 * @param zoom Zoom level to use.
 */
void OpenGLBackend::RenderOglSprite(const OpenGLSprite *gl_sprite, PaletteID pal, int x, int y, ZoomLevel zoom)
{
	/* Set textures. */
	bool rgb = gl_sprite->BindTextures();
	_glActiveTexture(GL_TEXTURE0 + 1);
	_glBindTexture(GL_TEXTURE_1D, this->pal_texture);

	/* Set palette remap. */
	_glActiveTexture(GL_TEXTURE0 + 3);
	if (pal != PAL_NONE) {
		_glBindTexture(GL_TEXTURE_1D, OpenGLSprite::pal_tex);
		if (pal != this->last_sprite_pal) {
			/* Different remap palette in use, update texture. */
			_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, OpenGLSprite::pal_pbo);
			_glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

			_glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, 256, GetNonSprite(GB(pal, 0, PALETTE_WIDTH), SpriteType::Recolour) + 1);
			_glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 256, GL_RED, GL_UNSIGNED_BYTE, nullptr);

			_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

			this->last_sprite_pal = pal;
		}
	} else {
		_glBindTexture(GL_TEXTURE_1D, OpenGLSprite::pal_identity);
	}

	/* Set up shader program. Use display resolution for screen size when PP is active,
	 * since the cursor is drawn after post-processing onto the default framebuffer. */
	Dimension dim = gl_sprite->GetSize(zoom);
	float screen_w = (this->pp_active && this->pp_display_size.width > 0) ? (float)this->pp_display_size.width : (float)_screen.width;
	float screen_h = (this->pp_active && this->pp_display_size.height > 0) ? (float)this->pp_display_size.height : (float)_screen.height;
	_glUseProgram(this->sprite_program);
	_glUniform4f(this->sprite_sprite_loc, (float)x, (float)y, (float)dim.width, (float)dim.height);
	_glUniform1f(this->sprite_zoom_loc, (float)zoom);
	_glUniform2f(this->sprite_screen_loc, screen_w, screen_h);
	_glUniform1i(this->sprite_rgb_loc, rgb ? 1 : 0);
	_glUniform1i(this->sprite_crash_loc, pal == PALETTE_CRASH ? 1 : 0);

	_glBindVertexArray(this->vao_quad);
	_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}


/* static */ std::array<GLuint, OpenGLSprite::NUM_TEX> OpenGLSprite::dummy_tex{};
/* static */ GLuint OpenGLSprite::pal_identity = 0;
/* static */ GLuint OpenGLSprite::pal_tex = 0;
/* static */ GLuint OpenGLSprite::pal_pbo = 0;

/**
 * Create all common resources for sprite rendering.
 * @return True if no error occurred.
 */
/* static */ bool OpenGLSprite::Create()
{
	_glGenTextures(NUM_TEX, OpenGLSprite::dummy_tex.data());

	for (int t = TEX_RGBA; t < NUM_TEX; t++) {
		_glBindTexture(GL_TEXTURE_2D, OpenGLSprite::dummy_tex[t]);

		_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
		_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}

	_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	_glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

	/* Load dummy RGBA texture. */
	const Colour rgb_pixel(0, 0, 0);
	_glBindTexture(GL_TEXTURE_2D, OpenGLSprite::dummy_tex[TEX_RGBA]);
	_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, &rgb_pixel);

	/* Load dummy remap texture. */
	const uint pal = 0;
	_glBindTexture(GL_TEXTURE_2D, OpenGLSprite::dummy_tex[TEX_REMAP]);
	_glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, &pal);

	/* Create palette remap textures. */
	std::array<uint8_t, 256> identity_pal;
	std::iota(std::begin(identity_pal), std::end(identity_pal), 0);

	/* Permanent texture for identity remap. */
	_glGenTextures(1, &OpenGLSprite::pal_identity);
	_glBindTexture(GL_TEXTURE_1D, OpenGLSprite::pal_identity);
	_glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	_glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	_glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAX_LEVEL, 0);
	_glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	_glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	_glTexImage1D(GL_TEXTURE_1D, 0, GL_R8, 256, 0, GL_RED, GL_UNSIGNED_BYTE, identity_pal.data());

	/* Dynamically updated texture for remaps. */
	_glGenTextures(1, &OpenGLSprite::pal_tex);
	_glBindTexture(GL_TEXTURE_1D, OpenGLSprite::pal_tex);
	_glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	_glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	_glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAX_LEVEL, 0);
	_glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	_glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	_glTexImage1D(GL_TEXTURE_1D, 0, GL_R8, 256, 0, GL_RED, GL_UNSIGNED_BYTE, identity_pal.data());

	/* Pixel buffer for remap updates. */
	_glGenBuffers(1, &OpenGLSprite::pal_pbo);
	_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, OpenGLSprite::pal_pbo);
	_glBufferData(GL_PIXEL_UNPACK_BUFFER, 256, identity_pal.data(), GL_DYNAMIC_DRAW);
	_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

	return _glGetError() == GL_NO_ERROR;
}

/** Free all common resources for sprite rendering. */
/* static */ void OpenGLSprite::Destroy()
{
	_glDeleteTextures(NUM_TEX, OpenGLSprite::dummy_tex.data());
	_glDeleteTextures(1, &OpenGLSprite::pal_identity);
	_glDeleteTextures(1, &OpenGLSprite::pal_tex);
	if (_glDeleteBuffers != nullptr) _glDeleteBuffers(1, &OpenGLSprite::pal_pbo);
}

/**
 * Create an OpenGL sprite with a palette remap part.
 * @param sprite_type The type of sprite to load.
 * @param sprite The sprite to create the OpenGL sprite for
 */
OpenGLSprite::OpenGLSprite(SpriteType sprite_type, const SpriteLoader::SpriteCollection &sprite)
{
	const auto &root_sprite = sprite.Root();
	this->dim.width = root_sprite.width;
	this->dim.height = root_sprite.height;
	this->x_offs = root_sprite.x_offs;
	this->y_offs = root_sprite.y_offs;

	int levels = sprite_type == SpriteType::Font ? 1 : to_underlying(ZoomLevel::End);
	assert(levels > 0);
	(void)_glGetError();

	this->tex = {};
	_glActiveTexture(GL_TEXTURE0);
	_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

	for (int t = TEX_RGBA; t < NUM_TEX; t++) {
		/* Sprite component present? */
		if (t == TEX_RGBA && root_sprite.colours == SpriteComponent::Palette) continue;
		if (t == TEX_REMAP && !root_sprite.colours.Test(SpriteComponent::Palette)) continue;

		/* Allocate texture. */
		_glGenTextures(1, &this->tex[t]);
		_glBindTexture(GL_TEXTURE_2D, this->tex[t]);

		_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
		_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
		_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		/* Set size. */
		for (int i = 0, w = this->dim.width, h = this->dim.height; i < levels; i++, w /= 2, h /= 2) {
			assert(w * h != 0);
			if (t == TEX_REMAP) {
				_glTexImage2D(GL_TEXTURE_2D, i, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
			} else {
				_glTexImage2D(GL_TEXTURE_2D, i, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, nullptr);
			}
		}
	}

	/* Upload texture data. */
	for (ZoomLevel zoom = ZoomLevel::Min; zoom <= (sprite_type == SpriteType::Font ? ZoomLevel::Min : ZoomLevel::Max); ++zoom) {
		const auto &src_sprite = sprite[zoom];
		this->Update(src_sprite.width, src_sprite.height, to_underlying(zoom), src_sprite.data);
	}

	assert(_glGetError() == GL_NO_ERROR);
}

/** Delete the textures we allocated. */
OpenGLSprite::~OpenGLSprite()
{
	_glDeleteTextures(NUM_TEX, this->tex.data());
}

/**
 * Update a single mip-map level with new pixel data.
 * @param width Width of the level.
 * @param height Height of the level.
 * @param level Mip-map level.
 * @param data New pixel data.
 */
void OpenGLSprite::Update(uint width, uint height, uint level, const SpriteLoader::CommonPixel * data)
{
	static ReusableBuffer<Colour> buf_rgba;
	static ReusableBuffer<uint8_t> buf_pal;

	_glActiveTexture(GL_TEXTURE0);
	_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	_glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

	if (this->tex[TEX_RGBA] != 0) {
		/* Unpack pixel data */
		size_t size = static_cast<size_t>(width) * height;
		Colour *rgba = buf_rgba.Allocate(size);
		for (size_t i = 0; i < size; i++) {
			rgba[i].r = data[i].r;
			rgba[i].g = data[i].g;
			rgba[i].b = data[i].b;
			rgba[i].a = data[i].a;
		}

		_glBindTexture(GL_TEXTURE_2D, this->tex[TEX_RGBA]);
		_glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, width, height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, rgba);
	}

	if (this->tex[TEX_REMAP] != 0) {
		/* Unpack and align pixel data. */
		size_t pitch = Align(width, 4);

		uint8_t *pal = buf_pal.Allocate(pitch * height);
		const SpriteLoader::CommonPixel *row = data;
		for (uint y = 0; y < height; y++, pal += pitch, row += width) {
			for (uint x = 0; x < width; x++) {
				pal[x] = row[x].m;
			}
		}

		_glBindTexture(GL_TEXTURE_2D, this->tex[TEX_REMAP]);
		_glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, width, height, GL_RED, GL_UNSIGNED_BYTE, buf_pal.GetBuffer());
	}

	assert(_glGetError() == GL_NO_ERROR);
}

/**
 * Query the sprite size at a certain zoom level.
 * @param level The zoom level to query.
 * @return Sprite size at the given zoom level.
 */
inline Dimension OpenGLSprite::GetSize(ZoomLevel level) const
{
	Dimension sd = { (uint)UnScaleByZoomLower(this->dim.width, level), (uint)UnScaleByZoomLower(this->dim.height, level) };
	return sd;
}

/**
 * Bind textures for rendering this sprite.
 * @return True if the sprite has RGBA data.
 */
bool OpenGLSprite::BindTextures() const
{
	_glActiveTexture(GL_TEXTURE0);
	_glBindTexture(GL_TEXTURE_2D, this->tex[TEX_RGBA] != 0 ? this->tex[TEX_RGBA] : OpenGLSprite::dummy_tex[TEX_RGBA]);
	_glActiveTexture(GL_TEXTURE0 + 2);
	_glBindTexture(GL_TEXTURE_2D, this->tex[TEX_REMAP] != 0 ? this->tex[TEX_REMAP] : OpenGLSprite::dummy_tex[TEX_REMAP]);

	return this->tex[TEX_RGBA] != 0;
}

/* Benchmark GPU timer query implementation. */

void OpenGLBackend::InitBenchmarkQueries()
{
	if (_glGenQueries == nullptr) return;
	_glGenQueries(2, this->benchmark_query);
	this->benchmark_query_idx = 0;
	this->benchmark_query_active = false;
	this->benchmark_query_pending = false;
	this->benchmark_gpu_ns = 0;
}

void OpenGLBackend::DestroyBenchmarkQueries()
{
	if (_glDeleteQueries == nullptr) return;
	if (this->benchmark_query[0] != 0) {
		_glDeleteQueries(2, this->benchmark_query);
		this->benchmark_query[0] = 0;
		this->benchmark_query[1] = 0;
	}
	this->benchmark_query_active = false;
	this->benchmark_query_pending = false;
}

void OpenGLBackend::BeginBenchmarkQuery()
{
	if (_glBeginQuery == nullptr || this->benchmark_query[0] == 0) return;
	_glBeginQuery(GL_TIME_ELAPSED, this->benchmark_query[this->benchmark_query_idx]);
	this->benchmark_query_active = true;
}

void OpenGLBackend::EndBenchmarkQuery()
{
	if (!this->benchmark_query_active) return;
	_glEndQuery(GL_TIME_ELAPSED);
	this->benchmark_query_active = false;
	this->benchmark_query_pending = true;
	/* Flip to the other query object so next frame writes to the alternate slot,
	 * and ReadBack reads the slot we just finished writing. */
	this->benchmark_query_idx = 1 - this->benchmark_query_idx;
}

/**
 * Read back the previous frame's GPU timer query result.
 * Uses double-buffered queries: we read the query from the previous frame
 * (the slot that is NOT currently being written to) to avoid stalling the GPU.
 * @return GPU time in nanoseconds for the post-processing pass, or 0 if unavailable.
 */
uint64_t OpenGLBackend::ReadBackBenchmarkGPUTime()
{
	if (!this->benchmark_query_pending || _glGetQueryObjectui64v == nullptr) return 0;

	/* Read the OTHER query (the one completed last frame, not the one in flight). */
	int read_idx = 1 - this->benchmark_query_idx;
	uint64_t ns = 0;
	_glGetQueryObjectui64v(this->benchmark_query[read_idx], GL_QUERY_RESULT, &ns);
	this->benchmark_gpu_ns = ns;
	this->benchmark_query_pending = false;

	return ns;
}

/* Bridge functions for benchmark.cpp (avoids exposing GL types outside the video layer). */

void BenchmarkGPUInit()
{
	if (OpenGLBackend::Get() != nullptr) OpenGLBackend::Get()->InitBenchmarkQueries();
}

void BenchmarkGPUDestroy()
{
	if (OpenGLBackend::Get() != nullptr) OpenGLBackend::Get()->DestroyBenchmarkQueries();
}

uint64_t BenchmarkGPUReadBack()
{
	if (OpenGLBackend::Get() != nullptr) return OpenGLBackend::Get()->ReadBackBenchmarkGPUTime();
	return 0;
}
