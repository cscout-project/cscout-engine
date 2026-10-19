/*
 * (C) Copyright 2001-2026 Diomidis Spinellis
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
 * For documentation read the corresponding .h file
 *
 */

#include <map>
#include <string>
#include <deque>
#include <vector>
#include <stack>
#include <iterator>
#include <iostream>
#include <fstream>
#include <list>
#include <set>

#include "cpp.h"
#include "debug.h"
#include "error.h"
#include "attr.h"
#include "metrics.h"
#include "fileid.h"
#include "tokid.h"
#include "token.h"
#include "parse.tab.h"
#include "ptoken.h"
#include "fchar.h"
#include "pltoken.h"
#include "macro.h"
#include "pdtoken.h"
#include "ctoken.h"
#include "type.h"
#include "stab.h"
#include "call.h"

/*
 * Return the character value of a string containing a C character
 * representation, advancing si to the next character to be processed.
 */
char
unescape_char(const string& s, string::const_iterator& si)
{
	int val;

	if (*si != '\\')
		return *si++;
	if (++si == s.end()) {
		/*
		 * @error
		 * A character escape sequence (<code>\c</code>) was not
		 * completed; no character follows the backslash
		 */
		Error::error(E_ERR, "Unexpected end in character escape sequence");
		return 0;
	}
	switch (val = *si++) {
	case '\'': return '\'';
	case '"': return '"';
	case '?': return '?';
	case '\\': return '\\';
	case 'a': return '\a';
	case 'b': return '\b';
	case 'e': return '\033'; // GCC extension
	case 'f': return '\f';
	case 'n': return '\n';
	case 'r': return '\r';
	case 't': return '\t';
	case 'v': return '\v';

	case '0': case '1': case '2': case '3':
	case '4': case '5': case '6': case '7':
		val -= '0';
		// Up to three digits maximum; strtol is useless.
		for (int i = 0; i < 2; i++) {
			if (si == s.end() || *si < '0' || *si > '7')
				break;
			val *= 8;
			val += *si++ - '0';
		}
		// if (si != s.end()) cout << "Next char is [" << *si << "]\n";
		return (val);
	case 'x': {
		string tmp(si - 2, s.end());
		char *endptr;
		tmp[0] = '0';		// For strtoul 0xddd
		// cout << "tmp=" << tmp << "\n";
		const char *cs = tmp.c_str();
		val = strtol(cs, &endptr, 0);
		if (endptr - cs == 1)
			// All we could read was 0x
			/*
			 * @error
			 * A hexadecimal character escape sequence
			 * <code>\x</code> continued with a non-hexadecimal
			 * character
			 */
			Error::error(E_ERR, "Illegal characters in hex escape sequence");
		si += endptr - cs - 2;
		// if (si != s.end()) cout << "Next char is [" << *si << "]\n";
		return (val);
	}

	default:
		/*
		 * @error
		 * A character escape sequence <code>\c</code> can not be
		 * recognised
		 */
		Error::error(E_ERR, "Invalid character escape sequence");
		return 0;
	}
}

// Create the map from identifiers to keyword token values
static map<string,int> &
make_keymap()
{
	static map<string,int> m;

	m["auto"] = AUTO; m["double"] = DOUBLE; m["int"] = INT;
	m["struct"] = STRUCT; m["break"] = BREAK; m["else"] = ELSE;
	m["long"] = LONG; m["switch"] = SWITCH; m["case"] = CASE;
	m["enum"] = ENUM; m["register"] = REGISTER; m["typedef"] = TYPEDEF;
	m["char"] = CHAR; m["extern"] = EXTERN; m["return"] = RETURN;
	m["union"] = UNION; m["const"] = TCONST; m["float"] = FLOAT;
	m["short"] = SHORT; m["unsigned"] = UNSIGNED; m["continue"] = CONTINUE;
	m["for"] = FOR; m["signed"] = SIGNED; m["void"] = TVOID;
	m["default"] = DEFAULT; m["goto"] = GOTO; m["sizeof"] = SIZEOF;
	m["volatile"] = VOLATILE; m["do"] = DO; m["if"] = IF;
	m["static"] = STATIC; m["while"] = WHILE;
	/* C99 */
	m["inline"] = INLINE;
	m["restrict"] = RESTRICT;
	m["_Bool"] = BOOL;
	m["_Complex"] = COMPLEX;
	m["_Imaginary"] = IMAGINARY;
	/* C11 */
	m["_Thread_local"] = THREAD_LOCAL;
	m["_Generic"] = GENERIC;
	m["_Alignas"] = ALIGNAS;
	m["_Noreturn"] = NORETURN;
	m["_Static_assert"] = STATIC_ASSERT;
	m["_Atomic"] = ATOMIC;
	/* C23 */
	m["typeof"] = TYPEOF;
	m["typeof_unqual"] = TYPEOF_UNQUAL;
	m["constexpr"] = CONSTEXPR;
	m["nullptr"] = NULLPTR;
	m["_Alignof"] = ALIGNOF;
	m["static_assert"] = STATIC_ASSERT;

	/* Microsoft */
	m["_asm"] = MSC_ASM;
	m["__try"] = TRY;
	m["__except"] = EXCEPT;
	m["__finally"] = FINALLY;
	m["__leave"] = LEAVE;
	/* gcc; from c-parse.in */
	m["__alignof"] = ALIGNOF;
	m["__alignof__"] = ALIGNOF;
	m["__asm"] = GNUC_ASM;
	m["__asm__"] = GNUC_ASM;
	m["__attribute"] = ATTRIBUTE;
	m["__attribute__"] = ATTRIBUTE;
	m["__auto_type"] = AUTO_TYPE;
	m["__builtin_choose_expr"] = CHOOSE_EXPR;
	m["__complex__"] = COMPLEX;
	m["__const"] = TCONST;
	m["__const__"] = TCONST;
	m["__imag__"] = IMAGINARY;
	m["__inline"] = INLINE;
	m["__inline__"] = INLINE;
	m["__label"] = LABEL;
	m["__label__"] = LABEL;
	m["__restrict"] = RESTRICT;
	m["__restrict__"] = RESTRICT;
	m["__signed"] = SIGNED;
	m["__signed__"] = SIGNED;
	m["__typeof"] = TYPEOF;
	m["__typeof__"] = TYPEOF;
	m["__typeof_unqual__"] = TYPEOF_UNQUAL;
	m["__volatile"] = VOLATILE;
	m["__volatile__"] = VOLATILE;
	// CScout
	m["__simd"] = SIMD; // SIMD basic types
	return m;
}

// Map from identifiers to keyword token values
static map<string,int>& keymap = make_keymap();

static int parse_lex_real();

/*
 * Consume tokens within an asm block
 * The block can be prefixed by identifiers like volatile (gcc)
 * Return the first token AFTER the block
 */
static Pdtoken
eat_block(int open, int close)
{
	Pdtoken t;

	// Eat leading space
	do {
		t.getnext();
	} while (t.get_code() == SPACE || t.get_code() == '\n');
	if (t.get_code() == open) {
		// Block-style
		if (DP())
			cout << "Starting block-style asm block\n";
		int matches = 1;
		do {
			t.getnext();
			if (t.get_code() == open)
				matches++;
			else if (t.get_code() == close)
				matches--;
		} while (matches > 0);
	} else {
		if (DP())
			cout << t << "Starting line-style asm block\n";
		/*
		 * Horrible MSC line-style abomination.
		 * These apparently terminate on a new-line, an __asm,
		 * or even a }
		 */
		for (;;) {
			t.getnext();
			switch (t.get_code()) {
			case '}':
			case '\n':
				return t;
			case IDENTIFIER:
				if (t.get_val() == "__asm")
					return t;
			}
		}
	}
	t.getnext();
	return t;
}

/*
 * Parse a gcc __attribute__(( ... )) block
 * looking for the keyword unused.  Return true if found.
 * We use a lexical scan, because it is extremely difficult to anticipate
 * all possible gcc attributes and their syntax.
 */
static bool
attribute_contains_unused()
{
	Pdtoken t;
	int matches = 0;
	bool found = false;
	bool opened = false;

	do {
		t.getnext();
		if (t.get_code() == IDENTIFIER &&
		    (t.get_val() == "unused" ||
		     t.get_val() == "__unused__"))
			found = true;

		if (t.get_code() == '(')
			matches++;
		else if (t.get_code() == ')')
			matches--;
		if (matches == 2)
			opened = true;
	} while (!(opened && matches == 0));

	return found;
}

// Lexical analysis function for yacc
static int
parse_lex_real()
{
	int c;
	Id const *id;
	map<string,int>::const_iterator ik;
	extern YYSTYPE parse_lval;
	extern bool parse_yacc_defs;

	Metrics::call_post_cpp_metrics(&Metrics::add_token);
	for (;;) {
		Pdtoken t;
		t.getnext();
again:
		switch (c = t.get_code()) {
		case '\n':
		case SPACE:
			continue;
		case EOF:
			return (0);
		case ABSFNAME:
		case PATHFNAME:
			Error::error(E_INTERNAL, "preprocessor filename, past preprocessor");
			continue;
		case PP_NUMBER:
			/* We need the value in the $x yacc variables */
			if (Fchar::is_yacc_file()) {
				parse_lval.t = identifier(t);
				return (INT_CONST);
			} else {
				const char *num;
				char *endptr;
				string s(t.get_val());
				num = s.c_str();

				unsigned long val = strtoul(num, &endptr, 0);
				if (is_int_suffix(endptr)) {
					parse_lval.t = basic(b_int);
					parse_lval.t.set_value(val);
					return (INT_CONST);
				} else {
					parse_lval.t = basic(b_float);
					return (FLOAT_CONST);
				}
			}
		case IDENTIFIER:
			if (DP()) cout << "id: [" << t.get_val() << "]\n";
			parse_lval.t = identifier(t);
			if (parse_yacc_defs)
				return (IDENTIFIER);
			ik = keymap.find(t.get_val());
			if (ik != keymap.end())
				// Keyword
				switch ((*ik).second) {
				case MSC_ASM:
					Pltoken::set_semicolon_line_comments(true);
					t = eat_block('{', '}');
					Pltoken::set_semicolon_line_comments(false);
					goto again;
				case ATTRIBUTE:
					if (attribute_contains_unused())
						return UNUSED;
					continue;
				default:
					return (*ik).second;
				}

			// Queue identifier for metrics processing
			Call::queue_post_cpp_identifier(t);
			Filedetails::queue_post_cpp_identifier(t);

			id = obj_lookup(t.get_val());
			if (id && id->get_type().is_typedef())
				return (TYPEDEF_NAME);	// Probably typedef
			return (IDENTIFIER);		// Plain identifier
		case '%':
			/* Recognize yacc reserved words */
			if (!parse_yacc_defs)
				return (c);
			t.getnext();
			if (t.get_code() != IDENTIFIER) {
				/*
				 * @error
				 * In the definitions section of a yacc file the
				 * % symbol was not followed by a legal yacc keyword
				 */
				Error::error(E_ERR, "% not followed by yacc keyword");
				return YBAD;
			}
			/* Ordered by frequency; good enough for yacc */
			if (t.get_val() == "type") return (YTYPE);
			if (t.get_val() == "token") return (YTOKEN);
			if (t.get_val() == "left") return (YLEFT);
			if (t.get_val() == "right") return (YRIGHT);
			if (t.get_val() == "nonassoc") return (YNONASSOC);
			if (t.get_val() == "prec") return (YPREC);
			if (t.get_val() == "start") return (YSTART);
			if (t.get_val() == "union") return (UNION);
			/*
			 * @error
			 * In the definitions section of a yacc file the
			 * % symbol was not followed by a legal yacc keyword
			 */
			Error::error(E_ERR, "% not followed by yacc keyword");
			return YBAD;
		case CHAR_LITERAL:
			/* We need the value in the $x yacc variables */
			if (Fchar::is_yacc_file())
				parse_lval.t = identifier(t);
			else {
				const string& s = t.get_val();
				string::const_iterator si;

				si = s.begin();
				parse_lval.t = basic(b_int);
				parse_lval.t.set_value(unescape_char(s, si));
			}
			return (CHAR_LITERAL);
		default:
			return (c);
		}
	}
}

int
Ctoken::lookup_keyword(const string& s)
{
	auto ik = keymap.find(s);
	if (ik == keymap.end())
		return -1;
	return ik->second;
}

bool
Ctoken::define_keyword(const string& name, const string& existing)
{
	auto ik = keymap.find(existing);
	if (ik == keymap.end())
		return false;
	keymap[name] = ik->second;
	return true;
}

// Lexical analysis function for yacc
// Used for debugging
int
parse_lex()
{
	int l = parse_lex_real();
	if (DP())
		cout << "Parse lex returns " << Token(l) << "\n";
	return (l);
}
