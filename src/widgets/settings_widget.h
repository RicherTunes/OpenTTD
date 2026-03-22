/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file settings_widget.h Types related to the settings widgets. */

#ifndef WIDGETS_SETTINGS_WIDGET_H
#define WIDGETS_SETTINGS_WIDGET_H

/** Widgets of the #GameOptionsWindow class. */
enum GameOptionsWidgets : WidgetID {
	WID_GO_TAB_GENERAL,            ///< General tab.
	WID_GO_TAB_GRAPHICS,           ///< Graphics tab.
	WID_GO_TAB_SOUND,              ///< Sound tab.
	WID_GO_TAB_SOCIAL,             ///< Social tab.
	WID_GO_TAB_ADVANCED,           ///< Advanced tab.
	WID_GO_TAB_SELECTION,          ///< Background of the tab selection.
	WID_GO_CURRENCY_DROPDOWN,      ///< Currency dropdown.
	WID_GO_DISTANCE_DROPDOWN,      ///< Measuring unit dropdown.
	WID_GO_AUTOSAVE_DROPDOWN,      ///< Dropdown to say how often to autosave.
	WID_GO_LANG_DROPDOWN,          ///< Language dropdown.
	WID_GO_RESOLUTION_DROPDOWN,    ///< Dropdown for the resolution.
	WID_GO_FULLSCREEN_BUTTON,      ///< Toggle fullscreen.
	WID_GO_FULLSCREEN_TEXT,        ///< Text for toggle fullscreen.
	WID_GO_GUI_SCALE,              ///< GUI Scale slider.
	WID_GO_GUI_SCALE_AUTO,         ///< Autodetect GUI scale button.
	WID_GO_GUI_SCALE_AUTO_TEXT,    ///< Text for Autodetect GUI scale.
	WID_GO_GUI_SCALE_BEVEL_BUTTON, ///< Toggle for chunky bevels.
	WID_GO_GUI_SCALE_BEVEL_TEXT,   ///< Text for chunky bevels.
	WID_GO_GUI_FONT_SPRITE,        ///< Toggle whether to prefer the sprite font over TTF fonts.
	WID_GO_GUI_FONT_SPRITE_TEXT,   ///< Text for sprite font toggle.
	WID_GO_GUI_FONT_AA,            ///< Toggle whether to anti-alias fonts.
	WID_GO_GUI_FONT_AA_TEXT,       ///< Text for anti-alias toggle.
	WID_GO_BASE_GRF_DROPDOWN,      ///< Use to select a base GRF.
	WID_GO_BASE_GRF_PARAMETERS,    ///< Base GRF parameters.
	WID_GO_BASE_GRF_CONTENT_DOWNLOAD, ///< 'Get Content' button for base GRF.
	WID_GO_BASE_GRF_OPEN_URL,      ///< Open base GRF URL.
	WID_GO_BASE_GRF_TEXTFILE,      ///< Open base GRF readme, changelog (+1) or license (+2).
	WID_GO_BASE_GRF_DESCRIPTION = WID_GO_BASE_GRF_TEXTFILE + TFT_CONTENT_END,     ///< Description of selected base GRF.
	WID_GO_BASE_SFX_DROPDOWN,      ///< Use to select a base SFX.
	WID_GO_BASE_SFX_CONTENT_DOWNLOAD, ///< 'Get Content' button for base SFX.
	WID_GO_TEXT_SFX_VOLUME,        ///< Sound effects volume label.
	WID_GO_BASE_SFX_VOLUME,        ///< Change sound effects volume.
	WID_GO_BASE_SFX_OPEN_URL,      ///< Open base SFX URL.
	WID_GO_BASE_SFX_TEXTFILE,      ///< Open base SFX readme, changelog (+1) or license (+2).
	WID_GO_BASE_SFX_DESCRIPTION = WID_GO_BASE_SFX_TEXTFILE + TFT_CONTENT_END,     ///< Description of selected base SFX.
	WID_GO_BASE_MUSIC_DROPDOWN,    ///< Use to select a base music set.
	WID_GO_BASE_MUSIC_CONTENT_DOWNLOAD, ///< 'Get Content' button for base music.
	WID_GO_TEXT_MUSIC_VOLUME,      ///< Music volume label.
	WID_GO_BASE_MUSIC_VOLUME,      ///< Change music volume.
	WID_GO_BASE_MUSIC_JUKEBOX,     ///< Open the jukebox.
	WID_GO_BASE_MUSIC_OPEN_URL,    ///< Open base music URL.
	WID_GO_BASE_MUSIC_TEXTFILE,    ///< Open base music readme, changelog (+1) or license (+2).
	WID_GO_BASE_MUSIC_DESCRIPTION = WID_GO_BASE_MUSIC_TEXTFILE + TFT_CONTENT_END, ///< Description of selected base music set.
	WID_GO_VIDEO_ACCEL_BUTTON,     ///< Toggle for video acceleration.
	WID_GO_VIDEO_ACCEL_TEXT,       ///< Text for video acceleration toggle.
	WID_GO_VIDEO_VSYNC_BUTTON,     ///< Toggle for video vsync.
	WID_GO_VIDEO_VSYNC_TEXT,       ///< Text for video vsync toggle.
	WID_GO_VIDEO_POST_PROCESSING_BUTTON, ///< Toggle for post-processing.
	WID_GO_VIDEO_POST_PROCESSING_TEXT,   ///< Text for post-processing toggle.
	WID_GO_VIDEO_RENDER_SCALE,           ///< Render scale slider.
	WID_GO_VIDEO_UPSCALE_DROPDOWN,       ///< Upscale mode dropdown.
	WID_GO_VIDEO_SHARPENING,             ///< Sharpening intensity slider.
	WID_GO_VIDEO_TEXTURE_FILTER_DROPDOWN, ///< Texture filtering dropdown.
	WID_GO_VIDEO_FXAA_BUTTON,            ///< Toggle for FXAA anti-aliasing.
	WID_GO_VIDEO_FXAA_TEXT,              ///< Text for FXAA toggle.
	WID_GO_VIDEO_BRIGHTNESS,             ///< Brightness slider.
	WID_GO_VIDEO_CONTRAST,               ///< Contrast slider.
	WID_GO_VIDEO_SATURATION,             ///< Saturation slider.
	WID_GO_VIDEO_NIGHT_MODE_BUTTON,      ///< Toggle for night mode.
	WID_GO_VIDEO_NIGHT_MODE_TEXT,        ///< Text for night mode toggle.
	WID_GO_VIDEO_CRT_FILTER_BUTTON,      ///< Toggle for CRT filter.
	WID_GO_VIDEO_CRT_FILTER_TEXT,        ///< Text for CRT filter toggle.
	WID_GO_VIDEO_COLOR_TEMPERATURE,      ///< Color temperature slider.
	WID_GO_VIDEO_VIGNETTE_BUTTON,        ///< Toggle for vignette effect.
	WID_GO_VIDEO_VIGNETTE_TEXT,          ///< Text for vignette toggle.
	WID_GO_VIDEO_VIGNETTE_INTENSITY,     ///< Vignette intensity slider.
	WID_GO_VIDEO_VIGNETTE_RADIUS,        ///< Vignette radius slider.
	WID_GO_VIDEO_VIGNETTE_SOFTNESS,      ///< Vignette softness slider.
	WID_GO_VIDEO_TILTSHIFT_BUTTON,       ///< Toggle for tilt-shift effect.
	WID_GO_VIDEO_TILTSHIFT_TEXT,         ///< Text for tilt-shift toggle.
	WID_GO_VIDEO_TILTSHIFT_FOCUS_Y,      ///< Tilt-shift focus Y slider.
	WID_GO_VIDEO_TILTSHIFT_FOCUS_WIDTH,  ///< Tilt-shift focus width slider.
	WID_GO_VIDEO_TILTSHIFT_BLUR,         ///< Tilt-shift blur strength slider.
	WID_GO_VIDEO_FILM_GRAIN_BUTTON,      ///< Toggle for film grain.
	WID_GO_VIDEO_FILM_GRAIN_TEXT,        ///< Text for film grain toggle.
	WID_GO_VIDEO_GRAIN_INTENSITY,        ///< Film grain intensity slider.
	WID_GO_VIDEO_NIGHT_INTENSITY,        ///< Night mode intensity slider.
	WID_GO_VIDEO_NIGHT_BLUE_SHIFT,       ///< Night mode blue shift slider.
	WID_GO_VIDEO_CRT_SCANLINES,          ///< CRT scanline intensity slider.
	WID_GO_VIDEO_CRT_CURVATURE,          ///< CRT curvature slider.
	WID_GO_VIDEO_CRT_ABERRATION,         ///< CRT chromatic aberration slider.
	WID_GO_VIDEO_DYNAMIC_LIGHTING_BUTTON, ///< Toggle for dynamic lighting.
	WID_GO_VIDEO_DYNAMIC_LIGHTING_TEXT,  ///< Text for dynamic lighting toggle.
	WID_GO_VIDEO_BLOOM_BUTTON,           ///< Toggle for bloom.
	WID_GO_VIDEO_BLOOM_TEXT,             ///< Text for bloom toggle.
	WID_GO_VIDEO_BLOOM_THRESHOLD,        ///< Bloom threshold slider.
	WID_GO_VIDEO_BLOOM_INTENSITY,        ///< Bloom intensity slider.
	WID_GO_VIDEO_WEATHER_DROPDOWN,       ///< Weather type dropdown.
	WID_GO_VIDEO_WEATHER_INTENSITY,      ///< Weather intensity slider.
	WID_GO_VIDEO_FXAA_PARAMS_SEL,        ///< Selection container for FXAA sub-parameters.
	WID_GO_VIDEO_FXAA_QUALITY,           ///< FXAA quality slider.
	WID_GO_VIDEO_FXAA_THRESHOLD,         ///< FXAA threshold slider.
	WID_GO_VIDEO_NIGHT_PARAMS_SEL,       ///< Selection container for night mode sub-parameters.
	WID_GO_VIDEO_CRT_PARAMS_SEL,         ///< Selection container for CRT sub-parameters.
	WID_GO_VIDEO_VIGNETTE_PARAMS_SEL,    ///< Selection container for vignette sub-parameters.
	WID_GO_VIDEO_TILTSHIFT_PARAMS_SEL,   ///< Selection container for tilt-shift sub-parameters.
	WID_GO_VIDEO_GRAIN_PARAMS_SEL,       ///< Selection container for film grain sub-parameter.
	WID_GO_VIDEO_BLOOM_PARAMS_SEL,       ///< Selection container for bloom sub-parameters.
	WID_GO_VIDEO_WEATHER_PARAMS_SEL,     ///< Selection container for weather sub-parameter.
	WID_GO_REFRESH_RATE_DROPDOWN,  ///< Dropdown for all available refresh rates.
	WID_GO_VIDEO_DRIVER_INFO,      ///< Label showing details about the current video driver.
	WID_GO_GRAPHICS_SCROLL,        ///< Scrollbar for the Graphics/Sound tab content.
	WID_GO_SURVEY_SEL,             ///< Selection to hide survey if no JSON library is compiled in.
	WID_GO_SURVEY_PARTICIPATE_BUTTON, ///< Toggle for participating in the automated survey.
	WID_GO_SURVEY_PARTICIPATE_TEXT, ///< Text for automated survey toggle.
	WID_GO_SURVEY_LINK_BUTTON,     ///< Button to open browser to go to the survey website.
	WID_GO_SURVEY_PREVIEW_BUTTON,  ///< Button to open a preview window with the survey results
	WID_GO_SOCIAL_PLUGINS,         ///< Main widget handling the social plugins.
	WID_GO_SOCIAL_PLUGIN_TITLE,    ///< Title of the frame of the social plugin.
	WID_GO_SOCIAL_PLUGIN_PLATFORM, ///< Platform of the social plugin.
	WID_GO_SOCIAL_PLUGIN_STATE,    ///< State of the social plugin.

	WID_GO_FILTER,             ///< Text filter.
	WID_GO_OPTIONSPANEL,       ///< Panel widget containing the option lists.
	WID_GO_SCROLLBAR,          ///< Scrollbar.
	WID_GO_SETTING_PROPERTIES, ///< Information area to display setting type and default value.
	WID_GO_HELP_TEXT,          ///< Information area to display help text of the selected option.
	WID_GO_HELP_TEXT_SCROLL,   ///< Scrollbar for setting description.
	WID_GO_EXPAND_ALL,         ///< Expand all button.
	WID_GO_COLLAPSE_ALL,       ///< Collapse all button.
	WID_GO_RESET_ALL,          ///< Reset all button.
	WID_GO_RESTRICT_CATEGORY,  ///< Label upfront to the category drop-down box to restrict the list of settings to show
	WID_GO_RESTRICT_TYPE,      ///< Label upfront to the type drop-down box to restrict the list of settings to show
	WID_GO_RESTRICT_DROPDOWN,  ///< The drop down box to restrict the list of settings
	WID_GO_TYPE_DROPDOWN,      ///< The drop down box to choose client/game/company/all settings

	WID_GO_SETTING_DROPDOWN = INVALID_WIDGET, ///< Dynamically created dropdown for changing setting value.
};

/** Widgets of the #CustomCurrencyWindow class. */
enum CustomCurrencyWidgets : WidgetID {
	WID_CC_RATE_DOWN,      ///< Down button.
	WID_CC_RATE_UP,        ///< Up button.
	WID_CC_RATE,           ///< Rate of currency.
	WID_CC_SEPARATOR_EDIT, ///< Separator edit button.
	WID_CC_SEPARATOR,      ///< Current separator.
	WID_CC_PREFIX_EDIT,    ///< Prefix edit button.
	WID_CC_PREFIX,         ///< Current prefix.
	WID_CC_SUFFIX_EDIT,    ///< Suffix edit button.
	WID_CC_SUFFIX,         ///< Current suffix.
	WID_CC_YEAR_DOWN,      ///< Down button.
	WID_CC_YEAR_UP,        ///< Up button.
	WID_CC_YEAR,           ///< Year of introduction.
	WID_CC_PREVIEW,        ///< Preview.
};

#endif /* WIDGETS_SETTINGS_WIDGET_H */
