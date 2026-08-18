/*
 * (C) Copyright 2001-2019 Diomidis Spinellis
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
 * A preprocessor directive token.
 * The getnext() method for these tokens performs preprocessor directives
 * on the lexical tokens.
 *
 */

#ifndef PDTOKEN_
#define PDTOKEN_

#include <string>
#include <deque>
#include <list>
#include <set>
#include <map>
#include <stack>
#include <vector>

using namespace std;

#include "ptoken.h"
#include "macro.h"
#include "fileid.h"
#include "compiledre.h"

class Pdtoken;

typedef list<Pdtoken> listPdtoken;

typedef deque<Ptoken> dequePtoken;
typedef list<Ptoken> PtokenSequence;
typedef set<string> setstring;
typedef map<string, PtokenSequence> mapArgval;
typedef stack<bool> stackbool;
typedef vector<string> vectorstring;
typedef vector<Pdtoken> vectorPdtoken;


class Macro;

typedef map<string, Macro> mapMacro;

class Pdtoken: public Ptoken {
private:
	static mapMacro macros;			// Defined macros
	static mapMacroBody macro_body_tokens;	// Tokens and the macros they belong to
	static PtokenSequence expand;		// Expanded input

	static bool at_bol;			// At beginning of line
	static stackbool iftaken;		// Taken #ifs
	static int skiplevel;			// Level of enclosing #ifs
	static bool output_defines;		// Output #defines on stdout

	static vectorstring include_path;	// Include file path
	static vectorPdtoken current_line;	// Currently read line
	static CompiledRE preprocessed_output_spec;// Files to preprocess
	static CompiledRE processed_files_spec; // Files to process

	static void process_directive();	// Handle a cpp directive
	static void eat_to_eol();		// Consume input including \n
	// Handle #include; next selects #include_next path-search semantics.
	static void process_include(bool next);
	static void process_define(bool ishard);// Handle a #define
	static void process_undef();		// Handle a #undef
	static void process_if();		// Handle #if
	static void process_ifdef(bool isndef);	// Handle #ifdef/ndef
	static void process_elif();		// Handle #elif
	static void process_else();		// Handle a #define
	static void process_endif();		// Handle a #define
	static void process_line();		// Handle a #line
	static void process_error(enum e_error_level e);
						// Handle a #error #warning
	static void process_pragma();		// Handle a #pragma

	// #pragma once support
	// Files that must be skipped rather than included
	static set<Fileid> skipped_includes;	// For #pragma once

	// Clear the list of files that shall not be included.
	// (At the start of a new compilation unit processing.)
	static void clear_skipped() { skipped_includes.clear(); }

	// Return true if the specified file shall not be included.
	// (After it has been processed with #pragma once.)
	static bool shall_skip(Fileid fid);

	// Add a file to the list of files that shall not be included.
	// (After encountering a #pragma once directive.)
	static void set_skip(Fileid fid) { skipped_includes.insert(fid); }
	// Get the next expanded token
	void getnext_expand();
public:
	// next means search after the current file's include-path entry.
	static bool can_find_include(const Ptoken& f, bool next);
	// Constructors
	Pdtoken(const Ptoken &pt) : Ptoken(pt) {};
	Pdtoken() {};
	// Get the next expanded token, maintainint the current line's contents
	void getnext();
	/*
	 * Get next token, while processing cpp directives, but without expanding macro
	 * identifiers.  This is used for gathering tokens inside macro arguments.  The
	 * C standard says that cpp directives in this mode lead to undefined behavior,
	 * but gcc CPP 3.2 and onward supports them doing the right thing.
	 * See http://developer.apple.com/documentation/DeveloperTools/gcc-4.0.1/cpp/Directives-Within-Macro-Arguments.html
	 */
	void getnext_noexpand();
	void getnext_noexpand_nospc();

	// Clear the defined macro table (when changing compilation unit)
	static void macros_clear() {
		macros.clear();
		macro_body_tokens.clear();
	}

	// Return the number of defined macros
	static mapMacro::size_type macros_size() {
		return macros.size();
	}

	// Find a macro given its name
	static mapMacro::const_iterator macros_find(const string& s) { return macros.find(s); }
	// Undefined macro returned by find
	static mapMacro::const_iterator macros_end() { return macros.end(); }
	// Given the result of macros_find return true of the macro is really defined
	static bool macro_is_defined(mapMacro::const_iterator mi) { return mi != macros.end() && (*mi).second.get_is_defined(); }
	// Add to the macros map an undefined macro
	static void create_undefined_macro(const Ptoken &name);
	// Add an element in the include path
	static void add_include(const string& s) { include_path.push_back(s); };
	// Clear the include path
	static void clear_include() { include_path.clear(); };
	// Called when we start processing a new file
	// or resume an old one.  We assume that files end in line
	// boundaries, even when they lack an explicit newline at their end
	static void file_switch() { at_bol = true; };
	// Return the macro where a given token resides
	static MCall *get_body_token_macro_mcall(Tokid t);
	// Return true if we are currently skipping due to conditional compilation
	static bool skipping() { return skiplevel != 0; }
	// Call this to output the #define code on stdout
	static void set_output_defines() { output_defines = true; }
	// Return currently read line
	static const vectorPdtoken& get_current_line() {
		return current_line;
	}
	// Set the RE on which to output preprocessed elements
	static void set_preprocessed_output(CompiledRE cre) {
		preprocessed_output_spec = cre;
	}
	// Set the RE of files to process
	static void set_processed_files(CompiledRE cre) {
		processed_files_spec = cre;
	}
};

ostream& operator<<(ostream& o,const dequePtoken &dp);
ostream& operator<<(ostream& o,const vectorPdtoken &dp);

#endif // PDTOKEN
