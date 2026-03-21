/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file upscale_plugin.cpp Runtime loading of external upscaling plugins (DLSS, FSR). */

#include "../stdafx.h"
#include "upscale_plugin.h"
#include "../debug.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "../safeguards.h"

static UpscalePluginAPI *_loaded_plugin = nullptr;

#ifdef _WIN32
static HMODULE _plugin_handle = nullptr;
#else
static void *_plugin_handle = nullptr;
#endif

/**
 * Load an upscaling plugin from a shared library.
 * @param path Path to the plugin DLL/SO.
 * @return Pointer to the plugin API, or nullptr on failure.
 */
UpscalePluginAPI *LoadUpscalePlugin(const std::string &path)
{
	/* Unload any existing plugin first. */
	UnloadUpscalePlugin();

	Debug(driver, 1, "Loading upscale plugin: {}", path);

#ifdef _WIN32
	_plugin_handle = LoadLibraryA(path.c_str());
	if (_plugin_handle == nullptr) {
		Debug(driver, 0, "Failed to load upscale plugin '{}': error {}", path, GetLastError());
		return nullptr;
	}

	auto get_plugin = reinterpret_cast<GetUpscalePluginFunc>(GetProcAddress(_plugin_handle, "GetUpscalePlugin"));
#else
	_plugin_handle = dlopen(path.c_str(), RTLD_NOW);
	if (_plugin_handle == nullptr) {
		Debug(driver, 0, "Failed to load upscale plugin '{}': {}", path, dlerror());
		return nullptr;
	}

	auto get_plugin = reinterpret_cast<GetUpscalePluginFunc>(dlsym(_plugin_handle, "GetUpscalePlugin"));
#endif

	if (get_plugin == nullptr) {
		Debug(driver, 0, "Upscale plugin '{}' missing GetUpscalePlugin entry point", path);
		UnloadUpscalePlugin();
		return nullptr;
	}

	_loaded_plugin = get_plugin();
	if (_loaded_plugin == nullptr) {
		Debug(driver, 0, "Upscale plugin '{}' returned null API", path);
		UnloadUpscalePlugin();
		return nullptr;
	}

	if (_loaded_plugin->api_version != UPSCALE_PLUGIN_API_VERSION) {
		Debug(driver, 0, "Upscale plugin '{}' has API version {} (expected {})",
			path, _loaded_plugin->api_version, UPSCALE_PLUGIN_API_VERSION);
		_loaded_plugin = nullptr;
		UnloadUpscalePlugin();
		return nullptr;
	}

	const char *name = _loaded_plugin->get_name != nullptr ? _loaded_plugin->get_name() : "unknown";
	const char *version = _loaded_plugin->get_version != nullptr ? _loaded_plugin->get_version() : "unknown";
	uint32_t caps = _loaded_plugin->get_capabilities != nullptr ? _loaded_plugin->get_capabilities() : 0;

	Debug(driver, 0, "Upscale plugin loaded: {} v{} (caps: 0x{:X})", name, version, caps);

	return _loaded_plugin;
}

/**
 * Unload the currently loaded upscaling plugin.
 */
void UnloadUpscalePlugin()
{
	if (_loaded_plugin != nullptr && _loaded_plugin->shutdown != nullptr) {
		_loaded_plugin->shutdown();
	}
	_loaded_plugin = nullptr;

#ifdef _WIN32
	if (_plugin_handle != nullptr) {
		FreeLibrary(_plugin_handle);
		_plugin_handle = nullptr;
	}
#else
	if (_plugin_handle != nullptr) {
		dlclose(_plugin_handle);
		_plugin_handle = nullptr;
	}
#endif
}

/**
 * Get the currently loaded upscaling plugin, or nullptr.
 */
UpscalePluginAPI *GetLoadedUpscalePlugin()
{
	return _loaded_plugin;
}
