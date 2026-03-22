/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file terrain_advanced_gui.cpp GUI for advanced terrain generation options. */

#include "stdafx.h"
#include "genworld.h"
#include "settings_type.h"
#include "strings_func.h"
#include "window_func.h"
#include "window_gui.h"
#include "dropdown_func.h"
#include "dropdown_type.h"

#include "table/strings.h"

#include "safeguards.h"

/** Widget IDs for the terrain advanced window. */
enum TerrainAdvancedWidgets : WidgetID {
	WID_TA_SMOOTHNESS_PULLDOWN,
	WID_TA_RIVER_PULLDOWN,
	WID_TA_LAKES_PULLDOWN,
	WID_TA_MOUNTAIN_RANGES_PULLDOWN,
	WID_TA_CONTINENT_SHAPE_PULLDOWN,
	WID_TA_TERRAIN_ALGO_PULLDOWN,
	WID_TA_BIOME_MODEL_PULLDOWN,
	WID_TA_TOWN_DISTRIBUTION_PULLDOWN,
};

static const StringID _smoothness[] = {STR_CONFIG_SETTING_ROUGHNESS_OF_TERRAIN_VERY_SMOOTH, STR_CONFIG_SETTING_ROUGHNESS_OF_TERRAIN_SMOOTH, STR_CONFIG_SETTING_ROUGHNESS_OF_TERRAIN_ROUGH, STR_CONFIG_SETTING_ROUGHNESS_OF_TERRAIN_VERY_ROUGH};
static const StringID _rivers[] = {STR_RIVERS_NONE, STR_RIVERS_FEW, STR_RIVERS_MODERATE, STR_RIVERS_LOT};
static const StringID _lakes[] = {STR_LAKES_NONE, STR_LAKES_FEW, STR_LAKES_NORMAL, STR_LAKES_MANY};
static const StringID _mountain_ranges[] = {STR_MOUNTAIN_RANGES_NONE, STR_MOUNTAIN_RANGES_FEW, STR_MOUNTAIN_RANGES_NORMAL, STR_MOUNTAIN_RANGES_MANY};
static const StringID _continent_shapes[] = {STR_CONTINENT_SHAPE_NONE, STR_CONTINENT_SHAPE_ISLAND, STR_CONTINENT_SHAPE_ARCHIPELAGO, STR_CONTINENT_SHAPE_FJORDS, STR_CONTINENT_SHAPE_SCATTERED, STR_CONTINENT_SHAPE_PENINSULA};
static const StringID _terrain_algorithms[] = {STR_TERRAIN_ALGORITHM_CLASSIC, STR_TERRAIN_ALGORITHM_IMPROVED_PERLIN};
static const StringID _biome_models[] = {STR_BIOME_MODEL_CLASSIC, STR_BIOME_MODEL_TEMPERATURE_BASED};
static const StringID _town_distributions[] = {STR_TOWN_DISTRIBUTION_RANDOM, STR_TOWN_DISTRIBUTION_EVEN};

static constexpr NWidgetPart _nested_terrain_advanced_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN), SetStringTip(STR_TERRAIN_ADVANCED_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN),
		NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_sparse, 0), SetPadding(WidgetDimensions::unscaled.sparse),
			NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(0, WidgetDimensions::unscaled.hsep_wide, 0),
				/* Labels column */
				NWidget(NWID_VERTICAL, NWidContainerFlag::EqualSize), SetPIP(0, WidgetDimensions::unscaled.vsep_sparse, 0),
					NWidget(WWT_TEXT, INVALID_COLOUR), SetStringTip(STR_TERRAIN_ADVANCED_SMOOTHNESS, STR_CONFIG_SETTING_ROUGHNESS_OF_TERRAIN_HELPTEXT), SetFill(1, 1),
					NWidget(WWT_TEXT, INVALID_COLOUR), SetStringTip(STR_TERRAIN_ADVANCED_RIVERS, STR_CONFIG_SETTING_RIVER_AMOUNT_HELPTEXT), SetFill(1, 1),
					NWidget(WWT_TEXT, INVALID_COLOUR), SetStringTip(STR_TERRAIN_ADVANCED_LAKES, STR_CONFIG_SETTING_LAKES_HELPTEXT), SetFill(1, 1),
					NWidget(WWT_TEXT, INVALID_COLOUR), SetStringTip(STR_TERRAIN_ADVANCED_MOUNTAIN_RANGES, STR_CONFIG_SETTING_MOUNTAIN_RANGES_HELPTEXT), SetFill(1, 1),
					NWidget(WWT_TEXT, INVALID_COLOUR), SetStringTip(STR_TERRAIN_ADVANCED_CONTINENT_SHAPE, STR_CONFIG_SETTING_CONTINENT_SHAPE_HELPTEXT), SetFill(1, 1),
					NWidget(WWT_TEXT, INVALID_COLOUR), SetStringTip(STR_TERRAIN_ADVANCED_TERRAIN_ALGORITHM, STR_CONFIG_SETTING_TERRAIN_ALGORITHM_HELPTEXT), SetFill(1, 1),
					NWidget(WWT_TEXT, INVALID_COLOUR), SetStringTip(STR_TERRAIN_ADVANCED_BIOME_MODEL, STR_CONFIG_SETTING_BIOME_MODEL_HELPTEXT), SetFill(1, 1),
					NWidget(WWT_TEXT, INVALID_COLOUR), SetStringTip(STR_TERRAIN_ADVANCED_TOWN_DISTRIBUTION, STR_CONFIG_SETTING_TOWN_DISTRIBUTION_HELPTEXT), SetFill(1, 1),
				EndContainer(),
				/* Dropdowns column */
				NWidget(NWID_VERTICAL, NWidContainerFlag::EqualSize), SetPIP(0, WidgetDimensions::unscaled.vsep_sparse, 0),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_TA_SMOOTHNESS_PULLDOWN), SetToolTip(STR_CONFIG_SETTING_ROUGHNESS_OF_TERRAIN_HELPTEXT), SetFill(1, 1),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_TA_RIVER_PULLDOWN), SetToolTip(STR_CONFIG_SETTING_RIVER_AMOUNT_HELPTEXT), SetFill(1, 1),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_TA_LAKES_PULLDOWN), SetToolTip(STR_CONFIG_SETTING_LAKES_HELPTEXT), SetFill(1, 1),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_TA_MOUNTAIN_RANGES_PULLDOWN), SetToolTip(STR_CONFIG_SETTING_MOUNTAIN_RANGES_HELPTEXT), SetFill(1, 1),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_TA_CONTINENT_SHAPE_PULLDOWN), SetToolTip(STR_CONFIG_SETTING_CONTINENT_SHAPE_HELPTEXT), SetFill(1, 1),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_TA_TERRAIN_ALGO_PULLDOWN), SetToolTip(STR_CONFIG_SETTING_TERRAIN_ALGORITHM_HELPTEXT), SetFill(1, 1),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_TA_BIOME_MODEL_PULLDOWN), SetToolTip(STR_CONFIG_SETTING_BIOME_MODEL_HELPTEXT), SetFill(1, 1),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_TA_TOWN_DISTRIBUTION_PULLDOWN), SetToolTip(STR_CONFIG_SETTING_TOWN_DISTRIBUTION_HELPTEXT), SetFill(1, 1),
				EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _terrain_advanced_desc(
	WDP_CENTER, {}, 0, 0,
	WC_GAME_OPTIONS, WC_GENERATE_LANDSCAPE,
	{},
	_nested_terrain_advanced_widgets
);

struct TerrainAdvancedWindow : public Window {
	TerrainAdvancedWindow(WindowDesc &desc) : Window(desc)
	{
		this->InitNested();
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		switch (widget) {
			case WID_TA_SMOOTHNESS_PULLDOWN:
				return GetString(_smoothness[_settings_newgame.game_creation.tgen_smoothness]);

			case WID_TA_RIVER_PULLDOWN:
				return GetString(_rivers[_settings_newgame.game_creation.amount_of_rivers]);

			case WID_TA_LAKES_PULLDOWN:
				return GetString(_lakes[_settings_newgame.game_creation.amount_of_lakes]);

			case WID_TA_MOUNTAIN_RANGES_PULLDOWN:
				return GetString(_mountain_ranges[_settings_newgame.game_creation.amount_of_mountain_ranges]);

			case WID_TA_CONTINENT_SHAPE_PULLDOWN: {
				uint8_t idx = static_cast<uint8_t>(_settings_newgame.game_creation.continent_shape);
				return GetString(_continent_shapes[std::min<uint8_t>(idx, lengthof(_continent_shapes) - 1)]);
			}

			case WID_TA_TERRAIN_ALGO_PULLDOWN: {
				uint8_t idx = static_cast<uint8_t>(_settings_newgame.game_creation.terrain_algorithm);
				return GetString(_terrain_algorithms[std::min<uint8_t>(idx, lengthof(_terrain_algorithms) - 1)]);
			}

			case WID_TA_BIOME_MODEL_PULLDOWN: {
				uint8_t idx = static_cast<uint8_t>(_settings_newgame.game_creation.biome_model);
				return GetString(_biome_models[std::min<uint8_t>(idx, lengthof(_biome_models) - 1)]);
			}

			case WID_TA_TOWN_DISTRIBUTION_PULLDOWN:
				return GetString(_town_distributions[static_cast<uint8_t>(_settings_newgame.game_creation.town_distribution)]);

			default:
				return this->Window::GetWidgetString(widget, stringid);
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_TA_SMOOTHNESS_PULLDOWN:
				ShowDropDownMenu(this, _smoothness, _settings_newgame.game_creation.tgen_smoothness, WID_TA_SMOOTHNESS_PULLDOWN, 0, 0);
				break;

			case WID_TA_RIVER_PULLDOWN:
				ShowDropDownMenu(this, _rivers, _settings_newgame.game_creation.amount_of_rivers, WID_TA_RIVER_PULLDOWN, 0, 0);
				break;

			case WID_TA_LAKES_PULLDOWN:
				ShowDropDownMenu(this, _lakes, _settings_newgame.game_creation.amount_of_lakes, WID_TA_LAKES_PULLDOWN, 0, 0);
				break;

			case WID_TA_MOUNTAIN_RANGES_PULLDOWN:
				ShowDropDownMenu(this, _mountain_ranges, _settings_newgame.game_creation.amount_of_mountain_ranges, WID_TA_MOUNTAIN_RANGES_PULLDOWN, 0, 0);
				break;

			case WID_TA_CONTINENT_SHAPE_PULLDOWN:
				ShowDropDownMenu(this, _continent_shapes, static_cast<uint8_t>(_settings_newgame.game_creation.continent_shape), WID_TA_CONTINENT_SHAPE_PULLDOWN, 0, 0);
				break;

			case WID_TA_TERRAIN_ALGO_PULLDOWN:
				ShowDropDownMenu(this, _terrain_algorithms, static_cast<uint8_t>(_settings_newgame.game_creation.terrain_algorithm), WID_TA_TERRAIN_ALGO_PULLDOWN, 0, 0);
				break;

			case WID_TA_BIOME_MODEL_PULLDOWN:
				ShowDropDownMenu(this, _biome_models, static_cast<uint8_t>(_settings_newgame.game_creation.biome_model), WID_TA_BIOME_MODEL_PULLDOWN, 0, 0);
				break;

			case WID_TA_TOWN_DISTRIBUTION_PULLDOWN:
				ShowDropDownMenu(this, _town_distributions, static_cast<uint8_t>(_settings_newgame.game_creation.town_distribution), WID_TA_TOWN_DISTRIBUTION_PULLDOWN, 0, 0);
				break;
		}
	}

	void OnDropdownSelect(WidgetID widget, int index, int) override
	{
		switch (widget) {
			case WID_TA_SMOOTHNESS_PULLDOWN:
				_settings_newgame.game_creation.tgen_smoothness = index;
				break;

			case WID_TA_RIVER_PULLDOWN:
				_settings_newgame.game_creation.amount_of_rivers = index;
				break;

			case WID_TA_LAKES_PULLDOWN:
				_settings_newgame.game_creation.amount_of_lakes = index;
				break;

			case WID_TA_MOUNTAIN_RANGES_PULLDOWN:
				_settings_newgame.game_creation.amount_of_mountain_ranges = index;
				break;

			case WID_TA_CONTINENT_SHAPE_PULLDOWN:
				_settings_newgame.game_creation.continent_shape = static_cast<ContinentShape>(index);
				break;

			case WID_TA_TERRAIN_ALGO_PULLDOWN:
				_settings_newgame.game_creation.terrain_algorithm = static_cast<TerrainAlgorithm>(index);
				break;

			case WID_TA_BIOME_MODEL_PULLDOWN:
				_settings_newgame.game_creation.biome_model = static_cast<BiomeModel>(index);
				break;

			case WID_TA_TOWN_DISTRIBUTION_PULLDOWN:
				_settings_newgame.game_creation.town_distribution = static_cast<TownDistribution>(index);
				break;
		}
		this->InvalidateData();
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;

		/* Disable terrain-specific settings when using original generator */
		bool is_original = _settings_newgame.game_creation.land_generator == 0;
		this->SetWidgetDisabledState(WID_TA_SMOOTHNESS_PULLDOWN, is_original);
		this->SetWidgetDisabledState(WID_TA_CONTINENT_SHAPE_PULLDOWN, is_original);
		this->SetWidgetDisabledState(WID_TA_TERRAIN_ALGO_PULLDOWN, is_original);
		this->SetWidgetDisabledState(WID_TA_MOUNTAIN_RANGES_PULLDOWN, is_original);
	}
};

void ShowTerrainAdvancedWindow()
{
	CloseWindowByClass(WC_GAME_OPTIONS);
	new TerrainAdvancedWindow(_terrain_advanced_desc);
}
