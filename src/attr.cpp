/*
 * (C) Copyright 2002-2026 Diomidis Spinellis
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
 * For documentation read the corresponding .h file
 *
 */

#include <map>
#include <string>
#include <deque>
#include <vector>
#include <stack>
#include <iterator>
#include <fstream>
#include <iostream>
#include <list>
#include <set>

#include "cpp.h"
#include "debug.h"
#include "error.h"
#include "attr.h"
#include "metrics.h"
#include "fileid.h"


// Leave space for a single project-attribute
vector<bool>::size_type Attributes::size = attr_end;

int Project::current_projid = attr_end;
int Project::next_projid = attr_end;
// Maps between ids and names
map<string, int> Project::projids;
vector<string> Project::projnames(attr_end);

// Keep this in sync with the enumeration
string Attributes::attribute_names[] = {
	"__attribute(__unused__)",
	"Identifier token from a macro",
	"Function-like macro",
	"Used in preprocessor constant",
	"Value used as preprocessor string operand",
	"Value defined as a C compile-time constant",
	"Value defined as not a C compile-time constant",
	"Value expanded as a C compile-time constant",
	"Value expanded as not a C compile-time constant",
	"Value used in C integer compile-time expression context",

	// User-visible attributes start here
	"Read-only",

	"Tag for struct/union/enum",
	"Member of struct/union",
	"Label",
	"Ordinary identifier",

	"Macro",
	"Undefined macro",
	"Undefed macro",
	"Macro redefined with same value",
	"Macro redefined with different value",
	"Macro argument",

	"File scope",
	"Project scope",

	"Typedef",
	"Enumeration constant",
	"Yacc identifier",
	"Function",
};

// Keep this in sync with the enumeration
string Attributes::attribute_short_names[] = {
	"__attribute(__unused__)",
	"idmtoken",
	"funmacro",
	"cppconst",
	"cppstrval",
	"defcconst",
	"defnotcconst",
	"expcconst",
	"expnotcconst",
	"iceuse",

	// User-visible attributes start here
	"ro",

	"tag",
	"member",
	"label",
	"obj",

	"macro",
	"umacro",
	"undefed",
	"redefed_same",
	"redefed_diff",
	"macroarg",

	"fscope",
	"pscope",

	"typedef",
	"enumconst",
	"yacc",
	"function",
};

void
Project::set_current_project(const string &name)
{
	map<string, int>::const_iterator p;
	if ((p = projids.find(name)) == projids.end()) {
		// Add new project
		current_projid = next_projid++;
		projnames.push_back(name);
		projids[name] = current_projid;
		if (Attributes::get_num_attributes() < projnames.size())
			Attributes::add_attribute();
	} else {
		current_projid = (*p).second;
	}
}
