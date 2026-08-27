/*
 * (C) Copyright 2002-2016 Diomidis Spinellis
 *
 * This file is part of CScout.
 *
 * CScout is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CScout is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CScout.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define __DATE__  "Jan 01 1970"
#define __TIME__ "12:34:56"
#define __FILE__ "UNKNOWN.c"
// Handled internally, but allow defined() to return true.
#define __LINE__ 1
#define __FUNCTION__ "UNKNOWN"
#define __func__ "UNKNOWN"
#define _Pragma(x)

/* To make it appear as a read-only identifier */
int main();

/* Avoid unused include file warnings */
static void _cscout_dummy1(void) { _cscout_dummy1(); }

/*
 * Some (e.g. Linux) #define static_assert (available on C23) through
 * _Static_assert. This results in its arguments being expanded before
 * the grammar sees the token to trigger the integer constant expression
 * expansion token. We disable this, by defining it here as an object macro.
 */
#pragma define_immutable static_assert static_assert
