/*
 * (C) Copyright 2001-2024 Diomidis Spinellis
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
 * A preprocessor macro definition.
 *
 */

#ifndef MACRO_
#define MACRO_

#include <deque>
#include <list>
#include <map>
#include <set>
#include <stack>

using namespace std;

#include "ptoken.h"

class Pdtoken;
class Macro;
class MCall;

typedef deque<Ptoken> dequePtoken;
typedef list<Ptoken> PtokenSequence;
typedef set<string> setstring;
typedef map<string, PtokenSequence> mapArgval;
typedef stack<bool> stackbool;
/*
 * We map to MCall * instead of Macro *, because Macro are stored
 * in a container and tend to move around.
 */
typedef map<Tokid, MCall *> mapMacroBody;

// Remove and return the operand beginning at position.
PtokenSequence gather_operand(PtokenSequence& tokens,
    PtokenSequence::iterator position, bool requires_parenthesis);

// A macro definition
class Macro {
public:
	// Typed options and arguments to the macro_expand function
	// Get more tokens or only use the supplied ones
	enum class TokenSourceOption { get_more, use_supplied };

	// Process preprocessor operators or protect their operands from expansion
	enum class OperatorHandling { process, skip };

	// Type of macro being substituted
	enum class MacroType { function_like, object_like };

	// Context in which macro_expand is called
	enum class CalledContext {
		process_c,		// From C code
		process_if,		// From #if #elif
		process_include,	// From #include file name
	};
private:
	Ptoken name_token;		// Name (used for unification)
	bool is_function;		// True if it is a function-macro
	bool is_immutable;		// Cannot be redefined or undefined
	bool is_vararg;			// True if the function has variable # of arguments (gcc)
	bool is_defined;		// True if the macro has been defined
					// Can be false through ifdef/ifndef/defined()
	dequePtoken formal_args;	// Formal arguments (names)
	dequePtoken value;		// Macro value
	MCall *mcall;			// Function call info
public:
	Macro( const Ptoken& name, bool id, bool is_function, bool is_immutable);
	// Accessor functions
	const Ptoken& get_name_token() const {return name_token; };
	void set_is_function(bool v) { is_function = v; };
	bool get_is_function() const { return is_function; };
	void set_is_vararg(bool v) { is_vararg = v; };
	bool get_is_defined() const { return is_defined; };
	bool get_is_vararg() const { return is_vararg; };
	bool get_is_immutable() const { return is_immutable; };
	const dequePtoken& get_formal_args() const { return formal_args; }
	const string get_name() const { return name_token.get_val(); }
	// Return the number of formal arguments
	int get_num_args() const { return formal_args.size(); }
	void form_args_push_back(Ptoken& t) { formal_args.push_back(t); };
	void value_push_back(Ptoken& t) { value.push_back(t); };
	MCall *get_mcall() const { return mcall; }
	const dequePtoken& get_value() const { return value; }

	// Remove trailing whitespace
	void value_rtrim();

	// Update the map to include the macro's body refering to the macro
	void register_macro_body(mapMacroBody &map) const;

	// Compare per ANSI C
	friend bool operator ==(const Macro& a, const Macro& b);
	inline friend bool operator !=(const Macro& a, const Macro& b) { return !(a == b); };
	// Print it (for debugging)
	friend ostream& operator<<(ostream& o,const Macro &m);

	friend PtokenSequence macro_expand(PtokenSequence ts, Macro::TokenSourceOption token_source, Macro::OperatorHandling operator_handling, Macro::CalledContext context);
	// Name-based comparison for the small set of visible macros
	// constructed in expand_macro.
	bool operator<(const Macro& other) const {
		return name_token < other.name_token;
	}
};
#endif // MACRO
