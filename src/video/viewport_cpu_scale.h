/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file viewport_cpu_scale.h Lightweight interface for CPU viewport scaling (no GL types). */

#ifndef VIDEO_VIEWPORT_CPU_SCALE_H
#define VIDEO_VIEWPORT_CPU_SCALE_H

bool IsViewportCPUScalingActive();
void *GetViewportCPUScratchBuffer();
int GetViewportCPUScratchWidth();
int GetViewportCPUScratchHeight();
int GetViewportCPUScratchPitch();

#endif /* VIDEO_VIEWPORT_CPU_SCALE_H */
