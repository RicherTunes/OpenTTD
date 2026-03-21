/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file tgp_func.h Internal functions for the TGP terrain generator, exposed for testing. */

#ifndef TGP_FUNC_H
#define TGP_FUNC_H

#include "tgp.h"

/** Fixed point type for heights */
using Height = int16_t;
static const int HEIGHT_DECIMAL_BITS = 4;

/**
 * Quintic smoothstep interpolation for improved Perlin noise.
 * f(t) = 6t^5 - 15t^4 + 10t^3
 * Properties: f(0)=0, f(1)=1, f(0.5)=0.5, f'(0)=f'(1)=0, f''(0)=f''(1)=0.
 * @param t The interpolation parameter, expected in [0, 1].
 * @return The smoothed value.
 */
static inline double QuinticSmoothstep(double t)
{
	return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

/**
 * Smoothed interpolation using quintic Hermite curve.
 * Drop-in replacement for LinearInterpolate with smoother transitions.
 * @param a The first value.
 * @param b The second value.
 * @param x The fraction between the two values.
 * @return The interpolated value.
 */
static inline double SmoothedInterpolate(double a, double b, double x)
{
	double t = QuinticSmoothstep(x);
	return a + t * (b - a);
}

#endif /* TGP_FUNC_H */
