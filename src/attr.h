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
 *
 * Equivalence class attributes.  Also used by token and tokid functions.
 *
 */

#ifndef ATTR_
#define ATTR_

#include <vector>
#include <string>
#include <map>

using namespace std;

// Attributes that can be set for an EC
// Keep in sync with attribute_names[] and short_names[]
// Update "Allowed attribute names" in cscout.1
// Consider updating workdb_schema, insert_eclass
enum e_attribute {
	is_declared_unused,	// Declared with __unused__ attribute
	is_macro_token,		// Identifier stored in a macro
				// Used to determine macro nesting
	// The following are used to determine object-like macros that
	// can be converted into C constants
	is_fun_macro,		// Defined as a function-like macro
	is_cpp_const,		// Used to derive a preprocessor constant
				// used in #if, #include, defined()
	is_cpp_str_val,		// Macro's value is used as a string (pasting
				// or stringization) in the preprocessor
	// Below are probable values; findings can include both cases.
	// Findings based on defined object-like macros.
	is_def_c_const,		// Value seen as a C compile-time constant.
	is_def_not_c_const,	// Value seen as not a C compile-time constant.
	// Findings based on expanded object-like macros.
	is_exp_c_const,		// Value seen as a C compile-time constant.
	is_exp_not_c_const,	// Value seen as not a C compile-time constant.
	is_used_in_ice,		// Value used in a C integer constant
				// expression (ICE) context

	// User-visible attributes start here
	is_readonly,		// Read-only; true if any member
				// comes from an ro file
	// The four C namespaces
	is_suetag,		// Struct/union/enum tag
	is_sumember,		// Struct/union member
	is_label,		// Goto label
	is_ordinary,		// Ordinary identifier

	is_macro,		// Name of an object or function-like macro
	is_undefined_macro,	// Never-defined macro (heuristic: ifdef, defined)
	is_undefed_macro,	// Macro that has been #undefed
	is_redefined_same_macro,// Macro redefined with same value
	is_redefined_diff_macro,// Macro redefined with different value
	is_macro_arg,		// Macro argument

	// The following are valid if is_ordinary is true:
	is_cscope,		// Compilation-unit (file) scoped
				// identifier  (static)
	is_lscope,		// Linkage-unit scoped identifier
	is_typedef,		// Typedef
	is_enumeration,		// Enumeration member
	is_yacc,		// Yacc identifier
	is_cfunction,		// Function

	attr_end,		// From here-on we store projects
	attr_begin = is_readonly // First user-visible attribute
};

class Attributes {
public:
	typedef vector<bool>::size_type size_type;
private:
	static size_type size;		// Size of the vector for all objects
	vector <bool> attr;		// Attributes and projects
					// Hopefully specialized to 1 bit/val
	void fix_size() { if (attr.size() < size) attr.resize(size, false); }
	static string attribute_names[];
	static string attribute_short_names[];
public:
	// Add another attribute (typically project)
	static void add_attribute() { size++; }
	// Return the number of active attributes
	static size_type get_num_attributes() { return size; }
	// Return the name given the enumeration member
	static const string &name(int n) { return attribute_names[n]; }
	// Return the short name given the enumeration member
	static const string &shortname(int n) { return attribute_short_names[n]; }
	Attributes() : attr(size, false) {}
	void set_attribute(int v) { fix_size(); attr[v] = true; }
	void set_attribute_val(int v, bool n) { fix_size(); attr[v] = n; }
	bool get_attribute(int v)
		{ fix_size(); return attr[v]; }
	// Return true if the set attributes specify an identifier
	bool is_identifier() {
		return
			attr[is_ordinary] ||
			attr[is_sumember] ||
			attr[is_suetag] ||
			attr[is_macro] ||
			attr[is_macro_arg] ||
			attr[is_undefed_macro] ||
			attr[is_label] ||
			attr[is_yacc];
	}
	void merge_with(Attributes &b) {
		this->fix_size();
		b.fix_size();
		for (size_type i = 0; i < size; i++)
			this->attr[i] = (b.attr[i] || this->attr[i]);
	}
};

class Project {
	// Current and next project-id
	static int current_projid;
	static int next_projid;
	// Maps between ids and names
	static map<string, int> projids;
	static vector<string> projnames;
public:
	// Set the name of the current project
	static void set_current_project(const string &name);
	// The a unique identifier for the current project
	static int get_current_projid() { return current_projid; }
	// Map access functions
	static int get_projid(const string &name) { return projids[name]; }
	static const string &get_projname(int i) { return projnames[i]; }
	// Return the map of all projects
	typedef map<string, int> proj_map_type;
	static const proj_map_type &get_project_map() { return projids; }
};

#endif /* ATTR_ */
