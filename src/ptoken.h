/*
 * (C) Copyright 2001-2025 Diomidis Spinellis
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
 * A preprocessor token.
 *
 *
 */

#ifndef PTOKEN_
#define PTOKEN_

#include <iostream>
#include <map>
#include <string>
#include <deque>
#include <vector>

using namespace std;

#include "cpp.h"
#include "fileid.h"
#include "tokid.h"
#include "token.h"
#include "parse.tab.h"

class Ctoken;
class Macro;

typedef set <Token> HideSet;

class Ptoken : public Token {
private:
	HideSet hideset;	// Hide set used for macro expansions

	// Macros that produced this token
	// The back() one is the caller when further replacements are made.
	vector<const Macro *> producers;
public:
	// Construct it based on the token code and the contents
	Ptoken(int icode, const string& ival)
		: Token(icode, ival) {};
	// Efficient constructor
	Ptoken() : Token() {}
	// Construct it from a CToken
	Ptoken(const Ctoken &t);

	// Copy ctor and assignment to propagate producer
	Ptoken(const Ptoken& other)
	    : Token(other),
	      hideset(other.hideset),
	      producers(other.producers)
	{}

	Ptoken& operator=(const Ptoken& other) {
		if (this != &other) {
			Token::operator=(other);
			hideset = other.hideset;
			producers = other.producers;
		}
		return *this;
	}

	// Accessor methods
	inline bool hideset_contains(const Ptoken &t) const { return (hideset.find(t) != hideset.end()); }
	inline void hideset_insert(Token t) { hideset.insert(t); }
	inline void hideset_insert(HideSet::const_iterator b, HideSet::const_iterator e) { hideset.insert(b, e); }
	inline const HideSet& get_hideset() const { return (hideset); }

	inline const Macro *get_producer() const {
		return producers.size() ? producers.back() : nullptr;
	}
	const vector<const Macro *> &get_producers() { return producers; }
	inline void add_producer(const Macro *m) { producers.push_back(m); }
	/*
	 * Set the is_cpp_str_val attribute for the macros that were
	 * expanded to yield the stringized or pasted token.
	 */
	void set_cpp_str_val() const;
	// Print it (for debugging)
	friend ostream& operator<<(ostream& o,const Ptoken &t);
	inline friend bool operator ==(const Ptoken& a, const Ptoken& b);
	// Return true if \n or SPACE
	inline bool is_space() const;
};

typedef list<Ptoken> PtokenSequence;

ostream& operator<<(ostream& o,const PtokenSequence &t);

bool operator ==(const Ptoken& a, const Ptoken& b)
{
	return (a.val == b.val);
}

inline bool Ptoken::is_space() const
{
	return (code == SPACE || code == '\n');
}

/*
 * Return true if (conservatively) the passed Ptoken sequence
 * can be a C compile-time constant (constant expression).
 * See ISO/IEC 9899:2017 section 6.6.
 * This implementation doesn't support enumeration constants, sizeof,
 * _Alignof_, which can be part of a C compile-time constant.
 */
template <typename TokenSequence> bool
is_c_const(const TokenSequence& ts)
{
	int bracket_nesting = 0;
	int n_question = 0;
	int n_colon = 0;

	for (const auto &tok : ts)
		switch (tok.get_code()) {
		case '(':
			bracket_nesting++;
			break;
		case ')':
			bracket_nesting--;
			break;
		case PP_NUMBER:
			// Check for floating point constant
			if (tok.get_val().find_first_of(".efEF")
			    != string::npos)
				return false;
		case SPACE:
		case CHAR_LITERAL:
		// Allowed operators in order of precedence
		case '+': case '-': case '~': case '!':
		// Binary versions of above unary: case '+': case '-':
		case '*': case '/': case '%':
		case LEFT_OP: case RIGHT_OP:
		case '<': case '>': case LE_OP: case GE_OP:
		case EQ_OP: case NE_OP:
		case '&':
		case '^':
		case '|':
		case AND_OP:
		case OR_OP:
			continue;
		/*
		 * Avoid unbalanced elements of ternary operators, as the
		 * following definition in the Linux kernel v6.19 
		 * #define LINE_LINES_POINT0_X 15:0
		 */
		case '?':
			n_question++;
			continue;
		case ':':
			n_colon++;
			continue;
		default:
			return false;
		}
	return bracket_nesting == 0 && n_question == n_colon;
}
#endif /* PTOKEN_ */
