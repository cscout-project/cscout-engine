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
 * Portions Copyright (c)  1989,  1990  James  A.  Roskind
 * Based on work by James A. Roskind; see comments at the end of this file.
 * Grammar obtained from http://www.empathy.com/pccts/roskind.html
 *
 * Declarations and type checking engine.
 * Note that for the purposes of this work we do not
 * need to keep precise track of types, esp. implicit arithmetic conversions.
 * Type checking is used:
 * 1) To identify the structure or union to use for member access
 * 2) As a sanity check for (1)
 * 3) To avoid mistakes caused by ommitting arbitrary part of the type checking
 *    mechanism
 * 4) To handle typedefs
 *
 *
 */

/* Leave the space at the end of the following line! */
%include ytoken.h 

%start file

%{
#include <iostream>
#include <string>
#include <fstream>
#include <deque>
#include <map>
#include <set>
#include <vector>
#include <list>

#include "parse.tab.h"

#include "cpp.h"
#include "debug.h"
#include "error.h"
#include "attr.h"
#include "metrics.h"
#include "fileid.h"
#include "tokid.h"
#include "fchar.h"		// get_fileid()
#include "eclass.h"
#include "token.h"
#include "ptoken.h"
#include "macro.h"
#include "pdtoken.h"
#include "pltoken.h"
#include "ctoken.h"
#include "type.h"
#include "stab.h"
#include "type2.h"
#include "debug.h"
#include "fdep.h"
#include "call.h"
#include "fcall.h"
#include "mcall.h"
#include "initializer.h"

void parse_error(const char *, ...)
{
	Error::error(E_ERR, "syntax error");
}

/*
 * According to ANSI C 99 6.2.5 paragraph 22:
 * A structure or union type of unknown content (as described in 6.7.2.3)
 * is an incomplete type. It is completed, for all declarations of that
 * type, by declaring the same structure or union tag with its defining
 * content later in the same scope.
 * Here we complete typedefs.  Member access of incomplete types is
 * handled in a similar manner in Tincomplete::member().
 */
static Type
completed_typedef(Type t)
{
	Id const *id = obj_lookup(t.get_name());
	csassert(id);	// If it's a typedef it can be found
	Token::unify(id->get_token(), t.get_token());
	if (id->get_type().is_incomplete()) {
		if (DP())
			cout << "Lookup for " << id->get_type().get_token().get_name() << "\n";
		const Id *id2 = tag_lookup(Block::get_scope_level(), id->get_type().get_token().get_name());
		if (id2)
			id = id2;
	}
	if (DP())
		cout << "The typedef type is " << id->get_type().clone() << "\n";
	return id->get_type().clone();
}

#define YYSTYPE_CONSTRUCTOR

// Elements used for parsing yacc code
/*
 * Lexical tie in
 * Set to true when we are parsing yacc definitions
 * (the first part of a yacc file, but not C code like
 * this part
 */
bool parse_yacc_defs;

// The symbol name of each rule RHS
static vector<string> yacc_dollar;

// The tag declared for each terminal or nonterminal symbol
typedef map<string, Type> YaccTypeMap;
static YaccTypeMap yacc_type;

// An appropriately typed yacc l/rvalue (int, or %union)
static Type yacc_stack;

// True if the %union mechanism is used
static bool yacc_typing;

/*
 * Types of yacc symbols
 * All yacc symbols (terminal and non-terminal) are entered into the yacc symbol table.
 * In addition, non-terminals are also defined as integer objects, so that they will
 * unify with the symbols appearing in the C code.
 */
enum e_yacc_symbol_type {
	y_terminal,		// Definitely a terminal symbol (e.g. EQUALS)
	y_nonterminal,		// Definitely a non-terminal symbol e.g. expr
	y_any			// Any of the two
};

// All declared yacc identifiers (terminal and non-terminal symbols)
static Stab yacc_identifier;

// Declare/use t as a yacc identifier of the specified type (see above)
static void
make_yacc_identifier(Type t, enum e_yacc_symbol_type ytype)
{
	if (DP())
		cout << "Make yacc identifier " << t.get_name() << endl;
	if (ytype == y_terminal) {
		Id const *id = obj_lookup(t.get_name());
		if (DP())
			cout << "Object lookup for " << t.get_name() << " returns " << id << "\n";
		if (id)
			Token::unify(id->get_token(), t.get_token());
		else
			obj_define(t.get_token(), basic(b_int, s_none, c_unspecified, sd_static, lk_internal));
	}
	/*
	 * Define t as a yacc identifier.
	 * If it is already defined, unify it with the previous definition.
	 */
	t.get_token().set_ec_attribute(is_yacc);

	Id const *id;
	if ((id = yacc_identifier.lookup(t.get_name())))
		Token::unify(id->get_token(), t.get_token());
	else
		yacc_identifier.define(t.get_token(), Type());
}

// Define the type of a given yacc name.
static void
yacc_type_define(Type name, Type type, enum e_yacc_symbol_type ytype)
{
	// Set the type of the token to its tag
	YaccTypeMap::const_iterator i = yacc_type.find(name.get_name());
	if (i == yacc_type.end() || !i->second.is_valid())
		yacc_type[name.get_name()] = type;
	// Declare the token as an integer
	if (name.get_token().get_code() == IDENTIFIER)
		make_yacc_identifier(name, ytype);
}
%}


%type <t> IDENTIFIER
%type <t> INT_CONST
%type <t> CHAR_LITERAL
%type <t> TYPEDEF_NAME
%type <t> identifier_or_typedef_name
%type <t> member_name
%type <t> constant
%type <t> primary_expression
%type <t> postfix_expression
%type <t> unary_expression
%type <t> cast_expression
%type <t> multiplicative_expression
%type <t> additive_expression
%type <t> shift_expression
%type <t> relational_expression
%type <t> equality_expression
%type <t> and_expression
%type <t> exclusive_or_expression
%type <t> inclusive_or_expression
%type <t> logical_and_expression
%type <t> logical_or_expression
%type <t> conditional_expression
%type <t> constant_expression
%type <t> constant_expression_opt
%type <t> assignment_expression
%type <t> string_literal_list
%type <t> comma_expression
%type <t> generic_selection
%type <t> generic_assoc_list
%type <t> generic_association

%type <t> basic_type_name
%type <t> storage_class
%type <t> declaration_qualifier_list
%type <t> type_qualifier_list
%type <t> type_qualifier_list_opt
%type <t> typeof_argument
%type <t> declaration_qualifier
%type <t> type_qualifier
%type <t> simple_type_qualifier
%type <t> function_specifier
%type <t> basic_declaration_specifier
%type <t> basic_type_specifier
%type <t> type_name
%type <t> type_specifier
%type <t> default_declaring_list
%type <t> declaring_list
%type <t> typedef_declaration_specifier
%type <t> typedef_type_specifier
%type <t> declaration_specifier
%type <t> sue_type_specifier
%type <t> sue_declaration_specifier
%type <t> member_declaration_list
%type <t> member_declaration
%type <t> member_default_declaring_list
%type <t> member_declaring_list
%type <t> member_declarator
%type <t> static_assert_declaration
%type <t> member_identifier_declarator
%type <t> elaborated_type_name
%type <t> aggregate_name
%type <t> enum_name
%type <t> old_function_declarator
%type <t> postfix_old_function_declarator

%type <t> declarator
%type <t> typedef_declarator
%type <t> parameter_typedef_declarator
%type <t> clean_typedef_declarator
%type <t> clean_postfix_typedef_declarator
%type <t> paren_typedef_declarator
%type <t> paren_postfix_typedef_declarator
%type <t> simple_paren_typedef_declarator
%type <t> paren_identifier_declarator
%type <t> array_qualifier
%type <t> array_abstract_declarator
%type <t> postfixing_abstract_declarator
%type <t> unary_abstract_declarator
%type <t> postfix_abstract_declarator
%type <t> abstract_declarator
%type <t> postfix_identifier_declarator
%type <t> unary_identifier_declarator
%type <t> identifier_declarator
%type <t> attributed_identifier_declarator
%type <t> aggregate_key
%type <t> range_expression
%type <t> enumerator_value_opt
%type <t> enumerator_list

%type <t> attribute
%type <t> attribute_list
%type <t> attribute_list_opt
%type <t> simple_type_qualifier_list
%type <t> simple_type_qualifier_list_opt
%type <t> assembly_decl
%type <t> asm_or_attribute_list

/* To allow compound statements as expressions (gcc extension) */
%type <t> statement
%type <t> statement_or_declaration
%type <t> any_statement
%type <t> statement_list
%type <t> compound_statement
%type <t> expression_statement
%type <t> comma_expression_opt
%type <t> identifier_list
%type <t> parameter_type_list
%type <t> parameter_list
%type <t> parameter_declaration

/* To handle __auto_type based on initializers */
%type <t> initializer_opt
%type <t> initializer
%type <t> initializer_open

/* Needed to avoid increasing nesting for expanded macros */
%type <t> IF
%type <t> SWITCH
%type <t> WHILE
%type <t> DO
%type <t> FOR

/* Needed for yacc */
%type <t> yacc_tag
%type <t> yacc_terminal_list_declaration
%type <t> yacc_type_list_declaration
%type <t> yacc_name_number
%type <t> yacc_name
%type <t> yacc_variable

%%

/* This refined grammar resolves several typedef ambiguities  in  the
draft  proposed  ANSI  C  standard  syntax  down  to  1  shift/reduce
conflict, as reported by a YACC process.  Note  that  the  one  shift
reduce  conflicts  is the traditional if-if-else conflict that is not
resolved by the grammar.  This ambiguity can  be  removed  using  the
method  described in the Dragon Book (2nd edition), but this does not
appear worth the effort.

There was quite a bit of effort made to reduce the conflicts to  this
level,  and  an  additional effort was made to make the grammar quite
similar to the C++ grammar being developed in  parallel.   Note  that
this grammar resolves the following ANSI C ambiguity as follows:

ANSI  C  section  3.5.6,  "If  the [typedef name] is redeclared at an
inner scope, the type specifiers shall not be omitted  in  the  inner
declaration".   Supplying type specifiers prevents consideration of T
as a typedef name in this grammar.  Failure to supply type specifiers
forced the use of the TYPEDEF_NAME as a type specifier.

ANSI C section 3.5.4.3, "In a parameter declaration, a single typedef
name in parentheses is  taken  to  be  an  abstract  declarator  that
specifies  a  function  with  a  single  parameter,  not as redundant
parentheses around the identifier".  This is extended  to  cover  the
following cases:

typedef float T;
int noo(const (T[5]));
int moo(const (T(int)));
...

Where  again the '(' immediately to the left of 'T' is interpreted as
being the start of a parameter type list,  and  not  as  a  redundant
paren around a redeclaration of T.  Hence an equivalent code fragment
is:

typedef float T;
int noo(const int identifier1 (T identifier2 [5]));
int moo(const int identifier1 (T identifier2 (int identifier3)));
...

*/


/* CONSTANTS */
constant:
        CHAR_LITERAL	/* Default rule */
        | INT_CONST	/* Default rule */
        | FLOAT_CONST	/* Not currently returned */
			{ $$ = basic(b_float); }
        /* We are not including ENUMERATIONconstant here  because  we
        are  treating  it like a variable with a type of "enumeration
        constant".  */
        ;

string_literal_list:
                STRING_LITERAL
			{ $$ = array_of(basic(b_char), CTConst()); }
                | string_literal_list STRING_LITERAL
			{ $$ = $1; }
                ;


/************************* EXPRESSIONS ********************************/
primary_expression:
        IDENTIFIER  /* We cannot use a typedef name as a variable */
			{
				Id const *id = obj_lookup($1.get_name());
				if (id) {
					Token::unify(id->get_token(), $1.get_token());
					// The value is modified as the expression is evaluated
					// so clone it to avoid changing it.
					$$ = id->get_type().clone();
					if (DP())
						cout << "Lookup " << $1.get_name() << " is " << $$ << "\n";
					if ($$.is_cfunction())
						FCall::register_call($1.get_token(), id);
				} else if ($1.get_name().compare(0, 10, "__builtin_") != 0) {
					/*
					 * @error
					 * An undeclared identifier was used
					 * in a primary expression
					 */
					Error::error(E_WARN, "undeclared identifier: " + $1.get_name());
					$$ = $1;
				} else
					$$ = $1;
			}
	| yacc_variable
			{
				if (!Fchar::is_yacc_file())
					/*
					 * @error
					 * A '$' token was encountered in C code.
					 * Values starting with a '$' token are only allowed inside
					 * yacc rules
					 */
					Error::error(E_FATAL, "Invalid C token: '$'");
				if (!yacc_typing)
					$$ = yacc_stack;
				else {
					Id const *id = yacc_stack.member($1.get_name());
					if (id) {
						$$ = id->get_type();
						if (DP())
							cout << ". returns " << $$ << "\n";
						csassert(id->get_name() == $1.get_name());
					} else {
						/*
						 * @error
						 * The member used in a $&lt;name&gt;X yacc construct
						 * was not defined as a %union member.
						 */
						Error::error(E_ERR, "%union does not have a member " + $1.get_name());
						$$ = basic(b_undeclared);
					}
				}
			}
        | constant
        | string_literal_list
        | NULLPTR               { $$ = pointer_to(basic(b_void)); }
        | '(' comma_expression ')'
			{ $$ = $2; }
	/* gcc extension */
	| '(' { Initializer::context_save(); } compound_statement ')'
		{
			$$ = $3;
			Initializer::context_restore();
		}

	/*
	 * Compound literal; C99 feature
	 * Example: (struct Point){ .x = 1, .y = 2 }
	 */
        | '(' type_name ')' { Initializer::context_save(); Initializer::expect($2); } braced_initializer
		{
			if (DP()) {
				cout << Fchar::get_path() << ':' << Fchar::get_line_num() << ": ";
				cout << "Type of compound literal " << $2 << "\n";
			}
			$$ = $2;
			Initializer::context_restore();
		}
	| generic_selection
        ;

postfix_expression:
        primary_expression
        | postfix_expression '[' comma_expression ']'
			{
				if ($1.is_subscriptable())
					$$ = $1.subscript();
				else
					/* Try the infamous 4[a] alternative */
					$$ = $3.subscript();
			}
        | postfix_expression '(' ')'
			{ $$ = $1.call(); }
        | postfix_expression '(' argument_expression_list ')'
			{ $$ = $1.call(); }
        | postfix_expression '.'   member_name
			{
				Id const *id = $1.member($3.get_name());
				if (id) {
					$$ = id->get_type();
					if (DP())
						cout << ". returns " << $$ << "\n";
					csassert(id->get_name() == $3.get_name());
					Token::unify(id->get_token(), $3.get_token());
				} else {
					/*
					 * @error
					 * The structure or union on the left
					 * of the
					 * <code>.</code> or
					 * <code>-&gt;</code> operator
					 * does not have as a member the
					 * identifier appearing on the
					 * operator's right
					 */
					Error::error(E_ERR, "structure or union does not have a member " + $3.get_name());
					$$ = basic(b_undeclared);
				}
			}
        | postfix_expression PTR_OP member_name
			{
				Id const *id = ($1.deref()).member($3.get_name());
				if (id) {
					$$ = id->get_type();
					csassert(id->get_name() == $3.get_name());
					Token::unify(id->get_token(), $3.get_token());
				} else {
					Error::error(E_ERR, "structure or union does not have a member " + $3.get_name());
					$$ = basic(b_undeclared);
				}
			}
        | postfix_expression INC_OP
			{ $$ = $1; }
        | postfix_expression DEC_OP
			{ $$ = $1; }
	/*
	 * gcc extension: __builtin_choose_expr
	 * This cannot be handled by a macro, because there are cases
	 * where macros are used to expand a single argument into two
	 * and pass these two to __builtin_choose_expr.
	 * Dynamically constructed arguments work after preprocessing,
	 * but not as part of preprocessing.
	 */
        | CHOOSE_EXPR '(' assignment_expression ',' assignment_expression ',' assignment_expression ')'
			{ $$ = $5; }
        ;

member_name:
        IDENTIFIER
        | TYPEDEF_NAME
        ; /* Default rules */

argument_expression_list:
        assignment_expression
        | argument_expression_list ',' assignment_expression
        ;

unary_expression:
        postfix_expression
        | INC_OP unary_expression
			{ $$ = $2; }
        | DEC_OP unary_expression
			{ $$ = $2; }
        | '+' cast_expression
			{ $$ = $2; }
        | '-' cast_expression
			{
				$$ = $2;
				$$.set_value(-$2.get_value());
			}
        | '~' cast_expression
			{
				$$ = $2;
				$$.set_value(~$2.get_value());
			}
        | '!' cast_expression
			{
				$$ = $2;
				$$.set_value(!$2.get_value());
			}
        | '&' cast_expression
			{ $$ = pointer_to($2); }
        | '*' cast_expression
			{ $$ = $2.deref(); }
        | SIZEOF unary_expression
			{
				$$ = basic(b_int);
				auto s = $2.get_sizeof();
				if (s != 0)
					$$.set_value(CTConst(s));
			}
        | SIZEOF '(' type_name ')'
			{
				$$ = basic(b_int);
				auto s = $3.get_sizeof();
				if (s != 0)
					$$.set_value(CTConst(s));
			}
        | ALIGNOF unary_expression
			{
				$$ = basic(b_int);
				auto a = $2.get_alignof();
				if (a != 0)
					$$.set_value(CTConst(a));
			}
        | ALIGNOF '(' type_name ')'
			{
				$$ = basic(b_int);
				auto a = $3.get_alignof();
				if (a != 0)
					$$.set_value(CTConst(a));
			}
	/* gcc extension */
        | AND_OP identifier_or_typedef_name
		{ label_use($2.get_token()); }
        ;

cast_expression:
        unary_expression
        | '(' type_name ')' cast_expression
		{
			$$ = $2;
			$$.set_value($4.get_value());
			if (DP())
				cout << "cast to " << $2 << "\n";
		}
        ;

multiplicative_expression:
        cast_expression
        | multiplicative_expression '*' cast_expression
		{
			$$ = $1;
			$$.set_value($1.get_value() * $3.get_value());
		}
        | multiplicative_expression '/' cast_expression
		{
			$$ = $1;
			$$.set_value($1.get_value() / $3.get_value());
		}
        | multiplicative_expression '%' cast_expression
		{
			$$ = $1;
			$$.set_value($1.get_value() % $3.get_value());
		}
        ;

additive_expression:
        multiplicative_expression
        | additive_expression '+' multiplicative_expression
			{
				/* Propagate pointer property */
				if ($3.is_ptr())
					$$ = $3;
				else
					$$ = $1;
				$$.set_value($1.get_value() + $3.get_value());
			}
        | additive_expression '-' multiplicative_expression
			{
				if ($1.is_ptr() && $3.is_ptr())
					$$ = basic(b_int);
				else
					$$ = $1;
				$$.set_value($1.get_value() - $3.get_value());
			}
        ;

shift_expression:
        additive_expression
        | shift_expression LEFT_OP additive_expression
		{
			$$ = $1;
			$$.set_value($1.get_value() << $3.get_value());
		}
        | shift_expression RIGHT_OP additive_expression
		{
			$$ = $1;
			$$.set_value($1.get_value() >> $3.get_value());
		}
        ;

relational_expression:
        shift_expression
        | relational_expression '<' shift_expression
			{
				$$ = basic(b_int);
				$$.set_value($1.get_value() < $3.get_value());
			}
        | relational_expression '>' shift_expression
			{
				$$ = basic(b_int);
				$$.set_value($1.get_value() > $3.get_value());
			}
        | relational_expression LE_OP shift_expression
			{
				$$ = basic(b_int);
				$$.set_value($1.get_value() <= $3.get_value());
			}
        | relational_expression GE_OP shift_expression
			{
				$$ = basic(b_int);
				$$.set_value($1.get_value() >= $3.get_value());
			}
        ;

equality_expression:
        relational_expression
        | equality_expression EQ_OP relational_expression
			{
				$$ = basic(b_int);
				$$.set_value($1.get_value() == $3.get_value());
			}
        | equality_expression NE_OP relational_expression
			{
				$$ = basic(b_int);
				$$.set_value($1.get_value() != $3.get_value());
			}
        ;

and_expression:
        equality_expression
        | and_expression '&' equality_expression
		{
			$$ = $1;
			$$.set_value($1.get_value() & $3.get_value());
		}
        ;

exclusive_or_expression:
        and_expression
        | exclusive_or_expression '^' and_expression
		{
			$$ = $1;
			$$.set_value($1.get_value() ^ $3.get_value());
		}
        ;

inclusive_or_expression:
        exclusive_or_expression
        | inclusive_or_expression '|' exclusive_or_expression
		{
			$$ = $1;
			$$.set_value($1.get_value() | $3.get_value());
		}
        ;

logical_and_expression:
        inclusive_or_expression
        | logical_and_expression AND_OP inclusive_or_expression
		{
			$$ = basic(b_int);
			$$.set_value($1.get_value() && $3.get_value());
		}
        ;

logical_or_expression:
        logical_and_expression
        | logical_or_expression OR_OP logical_and_expression
		{
			$$ = basic(b_int);
			$$.set_value($1.get_value() || $3.get_value());
		}
        ;

conditional_expression:
        logical_or_expression
        | logical_or_expression '?' comma_expression ':' conditional_expression
			{
				/*
				 * A number of complicated rules specify the result's type
				 * See ANSI 6.3.15
				 * For our purpose it may be enough to check if one of the
				 * two is a basic type or a pointer to a void
				 * (and therefore conceivably 0, i.e. NULL)
				 * and the other a pointer, to select the pointer type.
				 */
				if (DP())
					cout << $1 << " ? " << $3 << " : " << $5 << '\n';
				if (($3.is_basic() || ($3.is_ptr() && $3.deref().is_void())) && $5.is_ptr())
					$$ = $5;
				else
					$$ = $3;
				if ($1.get_value().is_const()) {
					if ($1.get_value().get_int_value())
						$$.set_value($3.get_value());
					else
						$$.set_value($5.get_value());
				}
			}
        | logical_or_expression '?' ':' conditional_expression
			{
				/*
				 * gcc extension: second argument is optional, in that
				 * case the result is the first.
				 */
				if ($4.is_basic() && $1.is_ptr())
					$$ = $1;
				else
					$$ = $4;
				if ($1.get_value().is_const()) {
					if ($1.get_value().get_int_value())
						$$.set_value($1.get_value());
					else
						$$.set_value($4.get_value());
				}
			}
        ;

/* Assignment expressions are initializer_members */
assignment_expression:
        conditional_expression
		{
			Fdep::add_provider(Fchar::get_fileid());
			if (DP())
				Error::error(E_DEBUG, "Add provider for conditional_expression -> assignment_expression");
			$$ = $1;
		}
	/*
	 * $1 was unary expression.  Changed to cast_expression
	 * to allow the illegal construct "(int)a = 3" that gcc accepts.
	 * In any case, the existing form allowed "-(int)a = 3"
	 */
        | cast_expression assignment_operator assignment_expression
		{
			Fdep::add_provider(Fchar::get_fileid());
			if (DP())
				Error::error(E_DEBUG, "Add provider for cast_expression assignment_operator assignment_expression -> assignment_expression");
			$$ = $1;
		}
        ;

assignment_operator:
        '='
        | MUL_ASSIGN
        | DIV_ASSIGN
        | MOD_ASSIGN
        | ADD_ASSIGN
        | SUB_ASSIGN
        | LEFT_ASSIGN
        | RIGHT_ASSIGN
        | AND_ASSIGN
        | XOR_ASSIGN
        | OR_ASSIGN
        ;

comma_expression:
        assignment_expression
        | comma_expression ',' assignment_expression
			{ $$ = $3; }
        ;

constant_expression:
        conditional_expression
		{
			if (DP())
				cout << $1 << endl;
			$$ = $1;
		}
	/*
	 * gcc extension: __builtin_choose_expr
	 * This cannot be handled by a macro, because there are cases
	 * where macros are used to expand a single argument into two
	 * and pass these two to __builtin_choose_expr.
	 * Dynamically constructed arguments work after preprocessing,
	 * but not as part of preprocessing.
	 * Depending on its arguments it can yield a constant expression,
	 * so it appears here as well as in postfix_expression/
	 */
        | CHOOSE_EXPR { Pdtoken::ice_enter(); } '('  assignment_expression
	  ',' assignment_expression ',' assignment_expression
	  ')'
		{
			Pdtoken::ice_exit();
			$$ = $6;
		}
        ;

range_expression:
	constant_expression
	/* gcc extension */
        | constant_expression ELLIPSIS constant_expression
	/* In designators we only care about the last value */
		{ $$ = $3; }
	;

    /* The following was used for clarity */
comma_expression_opt:
        /* Nothing */
		{ $$ = basic(b_void); }
        | comma_expression
		{ $$ = $1; }
        ;

generic_selection:
	/*
	 * For an example see test/c/c43-generic.c
	 * XXX Here we should select the list element with the
	 * compatible type and return the type of its value.
	 * Implementing the full type compatibility specification
	 * (C11 6.2.7, 6.7.3, 6.7.6) may not be worth the effort.
	 * So fudge it and return the type of first value.
	 */
        GENERIC '(' assignment_expression ',' generic_assoc_list ')'
		{ $$ = $5; }
	;

generic_assoc_list:
	  generic_association
	| generic_assoc_list ',' generic_association
        ;

generic_association:
	  type_name ':' assignment_expression
	  	{ $$ = $3; }
	| DEFAULT ':' assignment_expression
	  	{ $$ = $3; }
	;


/******************************* DECLARATIONS *********************************/

    /* The following is different from the ANSI C specified  grammar.
    The  changes  were  made  to  disambiguate  typedef's presence in
    declaration_specifiers (vs.  in the declarator for redefinition);
    to allow struct/union/enum tag declarations without  declarators,
    and  to  better  reflect the parsing of declarations (declarators
    must be combined with declaration_specifiers ASAP  so  that  they
    are visible in scope).

    Example  of  typedef  use  as either a declaration_specifier or a
    declarator:

      typedef int T;
      struct S { T T;}; / * redefinition of T as member name * /

    Example of legal and illegal statements detected by this grammar:

      int; / * syntax error: vacuous declaration * /
      struct S;  / * no error: tag is defined or elaborated * /

    Example of result of proper declaration binding:

        int a=sizeof(a); / * note that "a" is declared with a type  in
            the name space BEFORE parsing the initializer * /

        int b, c[sizeof(b)]; / * Note that the first declarator "b" is
             declared  with  a  type  BEFORE the second declarator is
             parsed * /

    */

declaration:
        sue_declaration_specifier ';'
        | sue_type_specifier ';'
        | declaring_list ';'
        | default_declaring_list ';'
	/* gcc extension */
	| label_declaring_list ';'
        ;

// Common part is required to avoid trial parsing between the two
// static_assert_declaration rules
static_assert_begin:
	STATIC_ASSERT { Pdtoken::ice_enter(); } '('
	;

static_assert_declaration:
	static_assert_begin constant_expression ',' { Pdtoken::ice_exit(); }
	  string_literal_list ')' ';'
		{ $$ = basic(b_undeclared); }
	| static_assert_begin constant_expression ')'
	  { Pdtoken::ice_exit(); } ';'
		{ $$ = basic(b_undeclared); }
	;

    /* Note that if a typedef were  redeclared,  then  a  declaration
    specifier must be supplied */

/*
 * A declaring list without an explicit type (e.g. int).
 * This relies on the default type handling rules.
 * As such, it can't  redeclare typedef names and doesn't
 * need to support __auto_type.
 */
default_declaring_list:
	/* static volatile @ a[3] @ = { 1, 2, 3} */
        declaration_qualifier_list attributed_identifier_declarator
		{
			$2.set_abstract($1);
			$2.declare();
			Initializer::expect($2);
			if ($1.qualified_unused() || $2.qualified_unused())
				$2.get_token().set_ec_attribute(is_declared_unused);
		}
						 initializer_opt
		{ $$ = $1; /* Pass-on qualifier */ }
	/* volatile @ a[3] @ = { 1, 2, 3} */
        | type_qualifier_list attributed_identifier_declarator
		{
			$2.declare();
			Initializer::expect($2);
			if ($1.qualified_unused() || $2.qualified_unused())
				$2.get_token().set_ec_attribute(is_declared_unused);
		}
						 initializer_opt
		{ $$ = $1; /* Pass-on qualifier */ }
        | default_declaring_list ',' attributed_identifier_declarator
		{
			$3.set_abstract($1);
			$3.declare();
			Initializer::expect($3);
			if ($1.qualified_unused() || $3.qualified_unused())
				$3.get_token().set_ec_attribute(is_declared_unused);
		}
						 initializer_opt
		{ $$ = $1; /* Pass-on qualifier */ }
        ;

/* gcc extension */
label_declaring_list:
	LABEL label_name_list
	;

label_name_list:
        identifier_or_typedef_name
		{ local_label_define($1.get_token()); }
        | label_name_list ',' identifier_or_typedef_name
		{ local_label_define($3.get_token()); }
	;

declaring_list:
	/*
	 * static int @ x @ = 42
	 * __auto_type @ x @ = 42
	 * But also:
	 * static int @ FILE @ = 42 (note reuse of typedef name)
	 */
        declaration_specifier declarator
		// Establish declarator before parsing initializer
		{
			if (!$1.is_auto_type()) {
				$2.set_abstract($1);
				$2.declare();
				Initializer::expect($2);
			}
			if ($1.qualified_unused() || $2.qualified_unused())
				$2.get_token().set_ec_attribute(is_declared_unused);
		}
						 initializer_opt
		{
			if ($1.is_auto_type()) {
				if (DP())
					cout << "Set __auto_type to " << $4 << "\n";
				$2.set_abstract($4);
				$2.declare();
			}
			$$ = $1; /* Pass-on qualifier */
		}
	/* int @ FILE @ = 42 */
        | type_specifier declarator
		{
			if (!$1.is_auto_type()) {
				$2.set_abstract($1);
				$2.declare();
				Initializer::expect($2);
			}
			if ($1.qualified_unused() || $2.qualified_unused())
				$2.get_token().set_ec_attribute(is_declared_unused);
		}
						 initializer_opt
		{
			if ($1.is_auto_type()) {
				if (DP())
					cout << "Set __auto_type to " << $4 << "\n";
				$2.set_abstract($4);
				$2.declare();
			}
			$$ = $1; /* Pass-on qualifier */
		}
	/* int a @ , @ b @ = 42 */
        | declaring_list ',' declarator
		{
			// __auto_type not legal in this case
			$3.set_abstract($1);
			$3.declare();
			Initializer::expect($3);
			if ($1.qualified_unused() || $3.qualified_unused())
				$3.get_token().set_ec_attribute(is_declared_unused);
		}
						 initializer_opt
		{ $$ = $1; /* Pass-on qualifier */ }
        ;

/* Includes storage class */
declaration_specifier:
        basic_declaration_specifier          /* Arithmetic or void */
        | sue_declaration_specifier          /* struct/union/enum */
        | typedef_declaration_specifier      /* typedef */
        ; /* Default rules */

type_specifier:
        basic_type_specifier                 /* Arithmetic or void */
        | sue_type_specifier                 /* Struct/Union/Enum */
        | typedef_type_specifier             /* Typedef */
        ; /* Default rules */


/* e.g. typedef static volatile const */
declaration_qualifier_list:  /* const/volatile, AND storage class */
        storage_class
        | type_qualifier_list storage_class
			{ $$ = merge($1, $2); }
        | declaration_qualifier_list declaration_qualifier
			{ $$ = merge($1, $2); }
        ;

/* e.g. const volatile */
type_qualifier_list:
        type_qualifier
		{ $$ = $1; }
        | type_qualifier_list type_qualifier
		{ $$ = merge($1, $2); }
        ; /* default rules */

/* One of: static extern typedef register auto const volatile */
declaration_qualifier:
        storage_class
        | type_qualifier			/* const, volatile, restrict */
        ; /* default rules */

simple_type_qualifier:
        TCONST		{ $$ = basic(b_abstract, s_none, c_unspecified, sd_none, lk_none, q_const); }
        | VOLATILE	{ $$ = basic(b_abstract, s_none, c_unspecified, sd_none, lk_none, q_volatile); }
        | RESTRICT	{ $$ = basic(b_abstract, s_none, c_unspecified, sd_none, lk_none, q_restrict); }
        | COMPLEX   { $$ = basic(b_abstract, s_none, c_unspecified, sd_none, lk_none, q_complex);  }
        | IMAGINARY   { $$ = basic(b_abstract, s_none, c_unspecified, sd_none, lk_none, q_imaginary);  }
        | SIMD   { $$ = basic(b_abstract, s_none, c_unspecified, sd_none, lk_none, q_simd);  }
	| attribute
	| ALIGNAS '(' typeof_argument ')'
		{ $$ = basic(); }
	| ATOMIC '(' typeof_argument ')'
		{ $$ = basic(); }
	| ATOMIC
		{ $$ = basic(); }
        ;

type_qualifier:
        simple_type_qualifier
	| function_specifier			/* inline */
        ;

function_specifier:
	INLINE		{ $$ = basic(); }
	| NORETURN	{ $$ = basic(); }
	;

basic_declaration_specifier:      /* Storage Class+Arithmetic or void */
        declaration_qualifier_list basic_type_name	/* static, int */
			{ $$ = merge($1, $2); }
        | basic_type_specifier  storage_class		/* int, static */
			{ $$ = merge($1, $2); }
	/* static int, volatile */
        | basic_declaration_specifier declaration_qualifier
			{ $$ = merge($1, $2); }
	/* static long, int */
        | basic_declaration_specifier basic_type_name
			{ $$ = merge($1, $2); }
        ;

basic_type_specifier:
        basic_type_name            /* Arithmetic or void */
        | type_qualifier_list basic_type_name		/* const, int */
			{ $$ = merge($1, $2); }
        | basic_type_specifier type_qualifier		/* int, volatile */
			{ $$ = merge($1, $2); }
        | basic_type_specifier basic_type_name		/* long, int */
			{ $$ = merge($1, $2); }
        ;

sue_declaration_specifier:          /* Storage Class + struct/union/enum */
	/* static const @ struct foo {int a;} */
        declaration_qualifier_list  elaborated_type_name
		{ $$ = $2.clone(); $$.set_storage_class($1);}
        | sue_type_specifier        storage_class
		{ $$ = $1.clone(); $$.set_storage_class($2); }
        | sue_declaration_specifier declaration_qualifier
		{ $$ = merge($1, $2); }
        ;

sue_type_specifier:
        elaborated_type_name              /* struct/union/enum */
        | type_qualifier_list elaborated_type_name
		{ $$ = merge($2, $1); }
        | sue_type_specifier type_qualifier
		{ $$ = merge($1, $2); }
        ;


typedef_declaration_specifier:       /* Storage Class + typedef types */
        typedef_type_specifier          storage_class
		{
			$1.set_storage_class($2);
			$$ = $1;
		}
        | declaration_qualifier_list    TYPEDEF_NAME
		{
			$$ = completed_typedef($2);
			$$.set_storage_class($1);
			$$.add_qualifiers($1);
		}
        | declaration_qualifier_list TYPEOF '(' typeof_argument ')'
		{
			$$ = $4.clone();
			$$.clear_storage_class();
			$$.set_storage_class($1);
			$$.add_qualifiers($1);
		}
        | declaration_qualifier_list TYPEOF_UNQUAL '(' typeof_argument ')'
		{
			$$ = $4.clone();
			$$.clear_storage_class();
			$$.set_storage_class($1);
		}
        | typedef_declaration_specifier declaration_qualifier
		{
			$$ = merge($1, $2);
		}
        ;

typedef_type_specifier:              /* typedef types */
        TYPEDEF_NAME
		{
			$$ = completed_typedef($1);
			$$.clear_storage_class();
		}
        | type_qualifier_list    TYPEDEF_NAME
		{
			$$ = completed_typedef($2);
			$$.clear_storage_class();
			$$.add_qualifiers($1);
		}
	| TYPEOF '(' typeof_argument ')'
		{
			$$ = $3.clone();
			$$.clear_storage_class();
		}
	| TYPEOF_UNQUAL '(' typeof_argument ')'
		{
			$$ = $3.clone();
			$$.clear_storage_class();
		}
	| type_qualifier_list TYPEOF '(' typeof_argument ')'
		{
			$$ = $4.clone();
			$$.clear_storage_class();
			$$.add_qualifiers($1);
		}
	| type_qualifier_list TYPEOF_UNQUAL '(' typeof_argument ')'
		{
			$$ = $4.clone();
			$$.clear_storage_class();
		}
        | typedef_type_specifier type_qualifier
		{ $$ = merge($1, $2); }
        ;

typeof_argument: comma_expression
		| type_name
		;

storage_class:
        TYPEDEF		{ $$ = basic(b_abstract, s_none, c_typedef); }
        | EXTERN	{ $$ = basic(b_abstract, s_none, c_unspecified, sd_none, lk_external); }
        | STATIC	{ $$ = basic(b_abstract, s_none, c_unspecified, sd_static, lk_internal); }
        | AUTO		{ $$ = basic(b_abstract, s_none, c_unspecified, sd_auto); }
        | REGISTER	{ $$ = basic(b_abstract, s_none, c_unspecified, sd_auto); }
        | THREAD_LOCAL	{ $$ = basic(b_abstract, s_none, c_unspecified, sd_thread); }
        | CONSTEXPR     { $$ = basic(b_abstract, s_none, c_unspecified, sd_static); }
        ;

basic_type_name:
        INT		{ $$ = basic(b_int); }
        | CHAR		{ $$ = basic(b_char); }
        | SHORT		{ $$ = basic(b_short); }
        | LONG		{ $$ = basic(b_long); }
        | FLOAT		{ $$ = basic(b_float); }
        | DOUBLE	{ $$ = basic(b_double); }
        | SIGNED	{ $$ = basic(b_abstract, s_signed); }
        | UNSIGNED	{ $$ = basic(b_abstract, s_unsigned); }
        | TVOID		{ $$ = basic(b_void); }
        | BOOL		{ $$ = basic(b_bool); }
        | COMPLEX	{ $$ = basic(b_complex); }
        | IMAGINARY	{ $$ = basic(b_imaginary); }
        | AUTO_TYPE	{ $$ = basic(b_auto_type); }
        ;

elaborated_type_name:
        aggregate_name
        | enum_name
        ; /* Default rules */

aggregate_name:
	/* struct { int x, y; } */
        aggregate_key '{'  member_declaration_list '}'
		{
			Filedetails::get_post_cpp_metrics(Fchar::get_fileid()).add_aggregate();
			if (!$3.is_undeclared())
				$3.set_union((bool)$1.get_nparam());
			$$ = $3;

		}
	/* struct Point { int x, y; } */
        | aggregate_key identifier_or_typedef_name '{'  member_declaration_list '}'
		{
			Id const *id = tag_lookup($2.get_name());
			if (id)
				Token::unify(id->get_token(), $2.get_token());
			if (!$4.is_undeclared())
				$4.set_union((bool)$1.get_nparam());
			tag_define($2.get_token(), $4);
			Filedetails::get_post_cpp_metrics(Fchar::get_fileid()).add_aggregate();
			$$ = $4;
		}
	/* struct Point */
        | aggregate_key identifier_or_typedef_name
		{
			Id const *id = tag_lookup($2.get_name());
			if (id) {
				Token::unify(id->get_token(), $2.get_token());
				$$ = id->get_type();
				if (DP())
					cout << "lookup returns " << $$ << "\n";
			} else {
				$$ = incomplete($2.get_token(), Block::get_scope_level());
				if (!$$.is_undeclared())
					$$.set_union((bool)$1.get_nparam());
				tag_define($2.get_token(), $$);
			}
		}
	/* gcc extensions */
        | aggregate_key identifier_or_typedef_name '{'  /* EMPTY member_declaration_list */ '}'
		{
			Id const *id = tag_lookup($2.get_name());
			if (id)
				Token::unify(id->get_token(), $2.get_token());
			Filedetails::get_post_cpp_metrics(Fchar::get_fileid()).add_aggregate();
			$$ = struct_union();
			if (!$$.is_undeclared())
				$$.set_union((bool)$1.get_nparam());
			tag_define($2.get_token(), $$);
		}
        | aggregate_key '{'  /* EMPTY member_declaration_list */ '}'
		{
			Filedetails::get_post_cpp_metrics(Fchar::get_fileid()).add_aggregate();
			$$ = struct_union();
			if (!$$.is_undeclared())
				$$.set_union((bool)$1.get_nparam());
		}
        ;

/* We overload plist to return a value from this rule */
aggregate_key:
        STRUCT simple_type_qualifier_list_opt
		{ $$ = plist(0); }
        | UNION simple_type_qualifier_list_opt
		{ $$ = plist(1); }
        ;

member_declaration_list:
	member_declaration
        |  member_declaration_list member_declaration
		{
			if (DP()) {
				cout << "$1: " << $1 << "\n";
				cout << "$2: " << $2 << "\n";
			}
			// Avoid internal errors and ignore empty or
			// padding member declarations (GCC extension)
			// while preserving valid declarations.
			if ($1.is_valid() && $2.is_valid()) {
				$1.merge_with($2);
				$$ = $1;
			} else if ($1.is_valid())
				$$ = $1;
			else
				$$ = $2;
		}
        ;

member_declaration:
        member_declaring_list ';'
		{ $$ = $1; }
        | member_default_declaring_list ';'
		{ $$ = $1; }
	| static_assert_declaration
		{ $$ = basic(b_undeclared); }
	| ';'
		{ $$ = basic(b_undeclared); }
	;

member_default_declaring_list:        /* doesn't redeclare typedef */
	/* volatile @ a[3] */
        type_qualifier_list member_identifier_declarator
		{
			if ($2.is_valid()) { // Check against padding bit fields
				$2.set_abstract($1);
				$$ = struct_union($2.get_token(), $2.type(), $1);
			} else
				$$ = struct_union($1);
		}
	/* volatile a[3], b */
        | member_default_declaring_list ',' member_identifier_declarator
		{
			if ($3.is_valid()) { // Check against padding bit fields
				$3.set_abstract($1.get_default_specifier());
				$1.add_member($3.get_token(), $3.type());
			}
			$$ = $1;
		}
        ;

member_declaring_list:
	/* unsigned int @ *a[3] */
        type_specifier member_declarator
		{
			if ($2.is_valid()) { // Check against padding bit fields
				$2.set_abstract($1);
				$$ = struct_union($2.get_token(), $2.type(), $1);
			} else
				$$ = struct_union($1);
			if (DP())
				cout << "(out)member_declaring_list = " << $$ << "\n";
		}
        | member_declaring_list ',' member_declarator
		{
			if ($3.is_valid()) { // Check against padding bit fields
				$3.set_abstract($1.get_default_specifier());
				$1.add_member($3.get_token(), $3.type());
			}
			$$ = $1;
		}
	/* struct {int hi, low;} - gcc/msc extension (unnamed structs) */
        | type_specifier
	{
		if (DP())
			cout << "unnamed member: " << $1 << "\n";
		if ($1.is_su()) {
			$$ = struct_union(Tsu_unnamed(), $1);
			if (DP())
				cout << "(out)member_declaring_list = " << $$ << "\n";
		} else {
					/*
					 * @error
					 * Unnamed members within a member
					 * declaring list (e.g.
					 * <code>struct {int x, y;}</code>)
					 * can only be structures or unions.
					 * (gcc/Microsoft C extension).
					 */
			Error::error(E_ERR, "Only struct/union unnamed elements allowed");
			$$ = basic(b_undeclared);
		}
	}
        ;

member_declarator:
	/* *a[3] */
	/* a : 5 */
        declarator bit_field_size_opt
		{
			Pdtoken::ice_exit();
			Filedetails::get_post_cpp_metrics(Fchar::get_fileid()).add_amember();
			$$ = $1;
		}
        | bit_field_size
		/* Padding bit field */
		{ $$ = basic(b_padbit); }
        ;

member_identifier_declarator:
	/* a[3]; also typedef names */
        attributed_identifier_declarator bit_field_size_opt
		{
			Filedetails::get_post_cpp_metrics(Fchar::get_fileid()).add_amember();
			$$ = $1;
		}
        | bit_field_size
		/* Padding bit field */
		{ $$ = basic(b_padbit); }
        ;

bit_field_size_opt:
        /* nothing */
        | bit_field_size
        ;

bit_field_size:
        { Pdtoken::ice_enter(); } ':' constant_expression
		{
			Pdtoken::ice_exit();
		}
        ;

enum_name:
        ENUM simple_type_qualifier_list_opt '{' enumerator_list comma_opt '}'
		{
			Filedetails::get_post_cpp_metrics(Fchar::get_fileid()).add_enum();
			$$ = enum_tag();
		}
        | ENUM simple_type_qualifier_list_opt identifier_or_typedef_name '{' enumerator_list comma_opt '}'
		{
			Filedetails::get_post_cpp_metrics(Fchar::get_fileid()).add_enum();
			tag_define($3.get_token(), $$ = enum_tag());
		}
        | ENUM simple_type_qualifier_list_opt identifier_or_typedef_name
		{
			Id const *id = tag_lookup($3.get_name());
			if (id) {
				Token::unify(id->get_token(), $3.get_token());
				$$ = id->get_type();
				if (DP())
					cout << "lookup returns " << $$ << "\n";
			} else
				$$ = basic(b_undeclared);
		}
        ;

enumerator_list:
	/* EMPTY (Microsoft extension) */
			{
				$$ =  basic(b_int, s_none, c_enum);
				$$.set_value(CTConst());
			}
        | identifier_or_typedef_name enumerator_value_opt
			{
				$$ = $2;
				if (!$$.get_value().is_const())
					$$.set_value(0);
				obj_define($1.get_token(), $$.clone());
				if (DP())
					cout << $1.get_token() << " = " << $$ << endl;
				Filedetails::get_post_cpp_metrics(Fchar::get_fileid()).add_emember();
			}
        | enumerator_list ',' identifier_or_typedef_name enumerator_value_opt
			{
				if ($4.get_value().is_const())
					$$ = $4;
				else {
					$$ = $1;
					$$.set_value($1.get_value() + 1);
				}
				obj_define($3.get_token(), $$.clone());
				if (DP())
					cout << $3.get_token() << " = " << $$ << endl;
				Filedetails::get_post_cpp_metrics(Fchar::get_fileid()).add_emember();
			}
        ;

enumerator_value_opt:
        /* Nothing */
		{
			$$ =  basic(b_int, s_none, c_enum);
			$$.set_value(CTConst());
		}
        | { Pdtoken::ice_enter(); } '=' constant_expression
		{
			Pdtoken::ice_exit();
			$$ = $3;
			$$.set_storage_class(basic(b_int, s_none, c_enum));
		}
        ;

/* Common extension: enum lists ending with a comma */
comma_opt:
        /* Nothing */
        | ','
        ;

parameter_type_list:
        parameter_list
		{ $$ = $1; }
        | parameter_list ',' ELLIPSIS
		{ $$ = $1; }
        ;

parameter_list:
        parameter_declaration
			{ $$ = plist($1.is_void() ? 0 : 1); }
        | parameter_list ',' parameter_declaration
			{ $1.add_param(); $$ = $1; }
        ;

parameter_declaration:
	/* int */
        declaration_specifier
	/* int [] */
        | declaration_specifier abstract_declarator
		{ $2.set_abstract($1); $$ = $2; }
	/* int i[2] */
        | declaration_specifier attributed_identifier_declarator
		{
			$2.set_abstract($1);
			$2.declare();
			if ($1.qualified_unused() || $2.qualified_unused())
				$2.get_token().set_ec_attribute(is_declared_unused);
			$$ = $2;
		}
	/* int FILE */
        | declaration_specifier parameter_typedef_declarator
		{
			$2.set_abstract($1);
			$2.declare();
			if ($1.qualified_unused() || $2.qualified_unused())
				$2.get_token().set_ec_attribute(is_declared_unused);
			$$ = $2;
		}
	/* volatile */
        | declaration_qualifier_list
	/* volatile int */
        | declaration_qualifier_list abstract_declarator
		{ $2.set_abstract($1); $$ = $2; }
	/* volatile int a */
        | declaration_qualifier_list attributed_identifier_declarator
		{
			$2.set_abstract($1);
			$2.declare();
			if ($1.qualified_unused() || $2.qualified_unused())
				$2.get_token().set_ec_attribute(is_declared_unused);
			$2.set_abstract($1);
			$$ = $2;
		}
	/* int */
        | type_specifier
        | type_specifier abstract_declarator
		{ $2.set_abstract($1); $$ = $2; }
        | type_specifier attributed_identifier_declarator
		{
			$2.set_abstract($1);
			$2.declare();
			if ($1.qualified_unused() || $2.qualified_unused())
				$2.get_token().set_ec_attribute(is_declared_unused);
			$$ = $2;
		}
        | type_specifier parameter_typedef_declarator
		{
			$2.set_abstract($1);
			$2.declare();
			if ($1.qualified_unused() || $2.qualified_unused())
				$2.get_token().set_ec_attribute(is_declared_unused);
			$$ = $2;
		}
        | type_qualifier_list
        | type_qualifier_list abstract_declarator
		{ $$ = merge($1, $2); }
        | type_qualifier_list attributed_identifier_declarator
		{
			$2.declare();
			if ($1.qualified_unused() || $2.qualified_unused())
				$2.get_token().set_ec_attribute(is_declared_unused);
			$2.set_abstract($1);
			$$ = $2;
		}
        ;

    /*  ANSI  C  section  3.7.1  states  "An identifier declared as a
    typedef name shall not be redeclared as a parameter".  Hence  the
    following is based only on IDENTIFIERs */

/* Only used for old-style function definitions, identifiers are declared
 * by default as int, but then a full declaration can follow. */
identifier_list:
        IDENTIFIER
			{
				obj_define($1.get_token(), basic(b_int));
				$$ = plist(1);
			}
        | identifier_list ',' IDENTIFIER
			{
				obj_define($3.get_token(), basic(b_int));
				$1.add_param();
				$$ = $1;
			}
        ;

identifier_or_typedef_name:
        IDENTIFIER
        | TYPEDEF_NAME
        ;

type_name:
        type_specifier
        | type_specifier abstract_declarator
		{ $2.set_abstract($1); $$ = $2; }
        | type_qualifier_list
		{ $$ = $1; }
        | type_qualifier_list abstract_declarator
		{ $$ = merge($1, $2); }
        ;

initializer_opt:
        /* nothing */
		{
			if (DP())
				cout << "Empty initializer\n";
			$$ = basic(b_abstract, s_none, c_unspecified, sd_none, lk_none, q_none);
		}
        | '=' initializer
		{
			if (DP())
				cout << "Initializer: " << $2 << "\n";
			$$ = $2;
		}
        ;

initializer_open:
	'{'
		/* Push (braced) TOS[pos] */
		{
			if (DP() && !Initializer::element_stack.empty()) {
				cout << Fchar::get_path() << ':' << Fchar::get_line_num() << ": ";
				cout << "Top initializer before initializer_open: " << ITOS;
			}
			Initializer::clear_used_elements();
			Type t(Initializer::element_stack.empty() ?
			    Initializer::upcoming_element :
			    ITOS.t.member(ITOS.pos));
			Initializer::element_stack.push(Initializer(t, true));
			if (DP()) {
				cout << "New initializer: " << ITOS;
				cout << "initializer type: " << t;
			}
			$$ = t;
		}
	;

initializer_close:
	'}'
		/* Pop stack up to and including first brace. */
		{
			while (!Initializer::element_stack.empty() && !ITOS.braced)
				Initializer::element_stack.pop();
			csassert(!Initializer::element_stack.empty() && ITOS.braced);
			Initializer::element_stack.pop();
			if (!Initializer::element_stack.empty()) {
				ITOS.pos++;
				if (DP())
					cout << "After initializer_close: " << ITOS;
			}
		}
	;

braced_initializer:
        initializer_open initializer_close
        | initializer_open initializer_list initializer_close
        | initializer_open initializer_list ',' initializer_close
        ;

initializer:
        initializer_open initializer_close
		{ $$ = basic(); }
        | initializer_open initializer_list initializer_close
		{ $$ = $1; }
        | initializer_open initializer_list ',' initializer_close
		{ $$ = $1; }
        | assignment_expression
		{
			// Remove from the stack all slots that can't hold this expression.
			Initializer::clear_used_elements();
			if (!Initializer::element_stack.empty()) {
				// Examine initializer top of stack (ITOS)
				if (ITOS.end.is_const() &&
				    ITOS.pos == ITOS.end.get_int_value()) {
					/*
					 * @error
					 * The members of an initializer list are more than the
					 * elements of the corresponding structure or array.
					 */
					Error::error(E_WARN, "too many initializers");
					if (DP())
						cout << ITOS << '\n';
				} else {
					/*
					 * XXX Here we should find an assignment compatible type.
					 * Because this is difficult (esp. taking into account
					 * scoping) we fudge it, and simply look for a type
					 * with the same number of elements.
					 */
					for (;;) {
						const Type &currt = ITOS.t.member(ITOS.pos);
						if (
						    // One case that fits: both are su with equal nelem
						    (currt.is_su() && $1.is_su() &&
						    $1.get_initializer_elements().get_int_value() == currt.get_initializer_elements().get_int_value()) ||
						    // Other case: string assignment
						    ($1.is_ptr() && $1.deref().is_char() && currt.is_ptr() && currt.deref().is_char()) ||
						    // Other case: non-struct initializer into non-struct non-array
						    (!$1.is_su() && !currt.is_su() && !currt.is_array())) {
							ITOS.pos++;
							ITOS.designated = false;
							if (DP()) cout << "Plain element assigned" << endl;
							break;
						} else if (currt.is_su() || currt.is_array()) {
							if (DP()) cout << "Failed to assign element: " << $1 << endl << " to slot: " << currt << endl << "Pushing ITOS[pos]" << endl;
							Initializer::element_stack.push(Initializer(ITOS.t.member(ITOS.pos), false));
						} else {
							/*
							 * The type of the initializer does not match
							 * the type of the initialized element.
							 */
							Error::error(E_ERR, "incompatible initializer");
							break;
						}
					}
				}
			}
			if (DP() && !Initializer::element_stack.empty())
				cout << "After assignment expression ITOS: " << ITOS;
			$$ = $1;
		}
        ;

initializer_list:
        initializer_member
        | initializer_list ',' initializer_member
        ;

initializer_member:
	initializer
	/* Do nothing, it will be handled by the underlying rules */
	| designator '=' initializer
	/* gcc extensions (argh!) */
	| designator initializer
	| member_name ':' initializer
		{
			Id const *id = ITOS.t.member($1.get_name());
			if (id) {
				csassert(id->get_name() == $1.get_name());
				Token::unify(id->get_token(), $1.get_token());
			} else
				Error::error(E_ERR, "structure or union does not have a member " + $1.get_name());
		}
	;

/* C99 feature */
designator:
	/*
	 * Range expression means fill all the range (inclusively!)
	 * with a single scalar value (gcc extension).
	 * Therefore, it is enough to advance to the range's
	 * last element.
	 */
        { Pdtoken::ice_enter(); } '[' range_expression ']'
		{
			Pdtoken::ice_exit();
			/* Pop unbraced stack elements. Set pos of TOS to $2. */
			while (!Initializer::element_stack.empty() && !ITOS.braced)
				Initializer::element_stack.pop();
			if (Initializer::element_stack.empty())
				Error::error(E_ERR, "designator does not appear in a braced initializer");
			else
				if ($3.get_value().is_const())
					ITOS.pos = $3.get_value().get_int_value();
				else
					Error::error(E_WARN, "unable to evaluate designator's compile-time constant");
		}
        | '.' member_name
		{
			/* Pop unbraced stack elements. Set pos of TOS to member_name ordinal. */
			while (!Initializer::element_stack.empty() && !ITOS.braced)
				Initializer::element_stack.pop();
			if (Initializer::element_stack.empty())
				Error::error(E_ERR, "designator does not appear in a braced initializer");
			else {
				Id const *id = ITOS.t.member($2.get_name());
				if (id) {
					Initializer::move_top_pos(id);
					csassert(id->get_name() == $2.get_name());
					Token::unify(id->get_token(), $2.get_token());
				} else
					Error::error(E_ERR, "structure or union does not have a member " + $2.get_name());
			}
		}
	/* See above for how we handle range expressions. */
        | designator { Pdtoken::ice_enter(); } '[' range_expression ']'
		{
			Pdtoken::ice_exit();
			/* Push (unbraced) TOS[pos] and set pos to $4. */
			csassert(!Initializer::element_stack.empty());
			Initializer::element_stack.push(Initializer(ITOS.t.member(ITOS.pos), false));
			if ($4.get_value().is_const())
				ITOS.pos = $4.get_value().get_int_value();
			else
				Error::error(E_WARN, "unable to evaluate designator's compile-time constant");
		}
        | designator '.' member_name
		{
			/* Push (unbraced) TOS[pos] and set pos to member_name ordinal. */
			Id const *id = ITOS.t.member(ITOS.pos).member($3.get_name());
			if (id) {
				csassert(id->get_name() == $3.get_name());
				Token::unify(id->get_token(), $3.get_token());
				csassert(!Initializer::element_stack.empty());
				Initializer::element_stack.push(Initializer(ITOS.t.member(ITOS.pos), false));
				Initializer::move_top_pos(id);
			} else
				Error::error(E_ERR, "structure or union does not have a member " + $3.get_name());
		}
	;


/*************************** STATEMENTS *******************************/
statement:
	any_statement
		{
			Fdep::add_provider(Fchar::get_fileid());
			if (DP())
				Error::error(E_DEBUG, "Add provider for any_statement -> statement");
			$$ = $1;
		}
	;

any_statement:
        labeled_statement { $$ = basic(b_void); }
        | compound_statement { $$ = basic(b_void); }
        | expression_statement { $$ = $1; }
        | selection_statement { $$ = basic(b_void); }
        | iteration_statement { $$ = basic(b_void); }
        | jump_statement { $$ = basic(b_void); }
        | try_statement { $$ = basic(b_void); }
        | assembly_statement { $$ = basic(b_void); } { $$ = basic(b_void); }
        | typed_function_definition { $$ = basic(b_void); } { $$ = basic(b_void); }
        ;

label:
        identifier_or_typedef_name ':' attribute_list_opt
		{ label_define($1.get_token()); }
        | { Pdtoken::ice_enter(); } CASE range_expression ':'
		{
			Pdtoken::ice_exit();
		}
        | DEFAULT ':'
        ;

labeled_statement:
	label statement
	;

function_brace_begin: '{'
		{
			Block::fn_body_enter();
		}
	;

brace_begin: '{'
		{ Block::enter(); }
	;

brace_end: '}'
		{ Block::exit(); }
	/* gcc extension */
	| label '}'
		{ Block::exit(); }
	;

compound_statement:
        brace_begin brace_end
		{ $$ = basic(b_void); }
        | brace_begin statement_list brace_end
		{ $$ = $2; }
        ;

function_body:
        function_brace_begin brace_end
		{ Call::unset_current_fun(); }
        | function_brace_begin statement_list brace_end
		{ Call::unset_current_fun(); }
        ;

declaration_list:
        declaration
        | declaration_list declaration
        ;

statement_or_declaration:
	declaration
		[ YYVALID; $$ = basic(b_void); ]
	| statement
		[ YYVALID; $$ = $1; ]
	| static_assert_declaration
		[ YYVALID; $$ = basic(b_void); ]
	;

statement_list:
        statement_or_declaration
		{ $$ = $1; }
        | statement_list statement_or_declaration
		{ $$ = $2; }
        ;

expression_statement:
        comma_expression_opt ';'
		{ $$ = $1; }
        ;

nested_statement:
	{ Call::increase_nesting(); } statement { Call::decrease_nesting(); }
	;

if_keyword: IF
		{ Call::check_macro_nesting($1.get_token()); }
	;

for_keyword: FOR
		{ Call::check_macro_nesting($1.get_token()); }
	;

selection_statement:
          if_keyword '(' comma_expression ')' nested_statement
        | if_keyword '(' comma_expression ')' nested_statement ELSE statement
		/*
		 * To avoid inflated nesting metrics from if else ... sequences
		 * the else body does not measure nesting level.
		 */
        | SWITCH { Call::check_macro_nesting($1.get_token()); } '(' comma_expression ')' nested_statement
        ;

iteration_statement:
        WHILE { Call::check_macro_nesting($1.get_token()); } '(' comma_expression ')' nested_statement
        | DO { Call::check_macro_nesting($1.get_token()); } nested_statement WHILE '(' comma_expression ')' ';'
        | for_keyword '(' comma_expression_opt ';' comma_expression_opt ';'
                comma_expression_opt ')' nested_statement
        | for_keyword '(' { Block::enter(); } declaring_list
	  ';' comma_expression_opt ';'
                comma_expression_opt ')' nested_statement
	          { Block::exit(); }
        ;

jump_statement:
        GOTO identifier_or_typedef_name ';'
		{ label_use($2.get_token()); }
	/* gcc extension */
        | GOTO '*' comma_expression ';'
        | CONTINUE ';'
        | BREAK ';'
        | RETURN comma_expression_opt ';'
	/* Microsoft extension */
        | LEAVE ';'
        ;

/* Microsoft __try __except __finally extensions */
try_statement:
	TRY compound_statement
		EXCEPT '(' comma_expression ')' compound_statement
	| TRY compound_statement FINALLY compound_statement
	;

/* Gcc __asm__  syntax */
assembly_decl:
	  GNUC_ASM type_qualifier_list_opt '(' string_literal_list ':' asm_operand_list_opt ')'
		{ $$ = $2; }
	| GNUC_ASM type_qualifier_list_opt '(' string_literal_list ')'
		{ $$ = $2; }
	| GNUC_ASM type_qualifier_list_opt '(' string_literal_list ':' asm_operand_list_opt ':' asm_operand_list_opt ')'
		{ $$ = $2; }
	| GNUC_ASM type_qualifier_list_opt '(' string_literal_list ':' asm_operand_list_opt ':' asm_operand_list_opt ':' asm_clobber_list_opt ')'
		{ $$ = $2; }
	;

assembly_statement:
	  GNUC_ASM type_qualifier_list_opt '(' string_literal_list ':' asm_operand_list_opt ')' ';'
	| GNUC_ASM type_qualifier_list_opt '(' string_literal_list ')' ';'
	| GNUC_ASM type_qualifier_list_opt '(' string_literal_list ':' asm_operand_list_opt ':' asm_operand_list_opt ')' ';'
	| GNUC_ASM type_qualifier_list_opt '(' string_literal_list ':' asm_operand_list_opt ':' asm_operand_list_opt ':' asm_clobber_list_opt ')' ';'
	| GNUC_ASM type_qualifier_list_opt GOTO '(' string_literal_list ':' asm_operand_list_opt ':' asm_operand_list_opt ':' asm_clobber_list_opt ':' asm_goto_label_list ')' ';'
	;

asm_operand_list_opt:
	/* Empty */
	| asm_operand_list
	;

asm_operand_list:
	asm_operand
	| asm_operand_list ',' asm_operand
	;

asm_operand: operand_name_opt string_literal_list '(' comma_expression ')'
	;

asm_clobber_list_opt:
	/* Empty */
	| asm_clobber_list
	;

asm_clobber_list:
	STRING_LITERAL
	| asm_clobber_list ',' STRING_LITERAL
	;

asm_goto_label_list:
        identifier_or_typedef_name
		{ label_use($1.get_token()); }
	| asm_goto_label_list ',' identifier_or_typedef_name
		{ label_use($3.get_token()); }
	;

/*
 * In GCC version 3.1, it is also possible to specify operands
 * using symbolic names which can be referenced within the assembler code.
 * These bear no relationship to the C identifiers.
 */
operand_name_opt:
	/* Empty */
	| '[' identifier_or_typedef_name ']'
	;

type_qualifier_list_opt:
	/* Empty */
		{ $$ = basic(); }
	| type_qualifier_list
	;

/***************************** EXTERNAL DEFINITIONS *****************************/

translation_unit:
        external_definition
        | translation_unit external_definition
        ;

external_definition:
        function_definition
			[ YYVALID; Function::exit(); Block::param_clear(); ]
        | declaration
			[ YYVALID; Block::param_clear(); ]
	| assembly_statement
	| ';'		/* Common extension - I believe */
	| static_assert_declaration
			[ YYVALID; Block::param_clear(); ]
        ;

function_definition:
	typed_function_definition
	| untyped_function_definition
	;

/*
 * A function definition with a type prefix.
 * Can be used for nested functions, without a conflict between
 * foo(a, b); and foo(a, b) {}
 */
typed_function_definition:
	/* foo(int a, int b) @ { } (and many illegal constructs) */
        declaration_specifier attributed_identifier_declarator
		{
			$2.set_abstract($1);
			$2.declare();
			if ($1.qualified_unused() || $2.qualified_unused())
				$2.get_token().set_ec_attribute(is_declared_unused);
			FCall::set_current_fun($2);
		}
					function_body
        | type_specifier attributed_identifier_declarator
		{
			$2.set_abstract($1);
			$2.declare();
			if ($1.qualified_unused() || $2.qualified_unused())
				$2.get_token().set_ec_attribute(is_declared_unused);
			FCall::set_current_fun($2);
		}
					function_body
        | declaration_qualifier_list attributed_identifier_declarator
		{
			$2.set_abstract($1);
			$2.declare();
			if ($1.qualified_unused() || $2.qualified_unused())
				$2.get_token().set_ec_attribute(is_declared_unused);
			FCall::set_current_fun($2);
		}
					function_body
        | type_qualifier_list attributed_identifier_declarator
		{
			$2.declare();
			if ($1.qualified_unused() || $2.qualified_unused())
				$2.get_token().set_ec_attribute(is_declared_unused);
			FCall::set_current_fun($2);
		}
					function_body

	/* foo(a, b) @ { } */
        | declaration_specifier old_function_declarator
		{
			$2.set_abstract($1);
			$2.declare();
			FCall::set_current_fun($2);
		}
					function_body
        | type_specifier             old_function_declarator
		{
			$2.set_abstract($1);
			$2.declare();
			FCall::set_current_fun($2);
		}
					function_body
        | declaration_qualifier_list old_function_declarator
		{
			$2.set_abstract($1);
			$2.declare();
			FCall::set_current_fun($2);
		}
					function_body
        | type_qualifier_list        old_function_declarator
		{
			$2.declare();
			FCall::set_current_fun($2);
		}
					function_body

	/* foo(a, b) @ int a; int b; @ { } */
        | declaration_specifier      old_function_declarator
		{ Block::param_use_begin(); } declaration_list
		{
			Block::param_use_end();
			$2.set_abstract($1);
			$2.declare();
			FCall::set_current_fun($2);
		}
					function_body
        | type_specifier             old_function_declarator
		{ Block::param_use_begin(); } declaration_list
		{
			Block::param_use_end();
			$2.set_abstract($1);
			$2.declare();
			FCall::set_current_fun($2);
		}
					function_body
        | declaration_qualifier_list old_function_declarator
		{ Block::param_use_begin(); } declaration_list
		{
			Block::param_use_end();
			$2.set_abstract($1);
			$2.declare();
			FCall::set_current_fun($2);
		}
					function_body
        | type_qualifier_list        old_function_declarator
		{ Block::param_use_begin(); } declaration_list
		{
			Block::param_use_end();
			$2.declare();
			FCall::set_current_fun($2);
		}
					function_body
         ;

untyped_function_definition:
	/* foo(int a, int b) @ { } (and many illegal constructs) */
	  attributed_identifier_declarator
		{
			$1.declare();
			if ($1.qualified_unused())
				$1.get_token().set_ec_attribute(is_declared_unused);
			FCall::set_current_fun($1);
		}
		function_body
	/* foo(a, b) @ { } */
        |                            old_function_declarator
		{
			$1.declare();
			FCall::set_current_fun($1);
		}
					function_body
	/* foo(a, b) @ int a; int b; @ { } */
        |                            old_function_declarator
		{ Block::param_use_begin(); } declaration_list
		{
			Block::param_use_end();
			$1.declare();
			FCall::set_current_fun($1);
		}
					function_body
         ;

declarator:
	/* *a[3] */
	  attributed_identifier_declarator
		{
			$$ = $1;
			if ($1.qualified_unused())
				$1.get_token().set_ec_attribute(is_declared_unused);
		}
        | typedef_declarator
        | typedef_declarator asm_or_attribute_list
		{
			$$ = $1;
			if ($1.qualified_unused() || $2.qualified_unused())
				$1.get_token().set_ec_attribute(is_declared_unused);
		}
        ;

simple_type_qualifier_list_opt:
	/* Empty */
		{ $$ = basic(); }
	| simple_type_qualifier_list
		{ $$ = $1; }
	;

simple_type_qualifier_list:
	simple_type_qualifier
		{ $$ = $1; }
	| simple_type_qualifier_list attribute
		{ $$ = merge($1, $2); }
	;

attribute_list_opt:
	/* Empty */
		{ $$ = basic(); }
	| attribute_list
		{ $$ = $1; }
	;

attribute_list:
	attribute
		{ $$ = $1; }
	| attribute_list attribute
		{
			if ($2.qualified_unused())
				$$ = $2;
			else
				$$ = $1;
		}
	;

attribute:
	/*
	 * Result of a gcc __attribute(...). This is handled lexically
	 * and may return UNUSED.
	 * register u_int64_t a0 @ __asm__("$16") = pfn; (alpha code)
	 * int enter(void) __asm__("enter");
	 */
	UNUSED
		{ $$ = basic(b_abstract, s_none, c_unspecified, sd_none, lk_none, q_unused); }
	;

asm_or_attribute_list:
	  attribute
	| assembly_decl
	| asm_or_attribute_list attribute
		{
			if ($2.qualified_unused())
				$$ = $2;
			else
				$$ = $1;
		}
	| asm_or_attribute_list assembly_decl
		{ $$ = $1; }
	;

typedef_declarator:
        paren_typedef_declarator          /* would be ambiguous as parameter */
        | parameter_typedef_declarator    /* not ambiguous as parameter */
        ; /* Default rules */

parameter_typedef_declarator:
        TYPEDEF_NAME
        | TYPEDEF_NAME postfixing_abstract_declarator
		{ $1.set_abstract($2); $$ = $1; }
        | clean_typedef_declarator
        ;

    /*  The  following have at least one '*'. There is no (redundant)
    '(' between the '*' and the TYPEDEF_NAME. */

clean_typedef_declarator:
        clean_postfix_typedef_declarator
        | '*' parameter_typedef_declarator
		{ $2.set_abstract(pointer_to(basic())); $$ = $2; }
        | '*' type_qualifier_list parameter_typedef_declarator
		{ $3.set_abstract(pointer_to($2)); $$ = $3; }
        ;

clean_postfix_typedef_declarator:
        '(' clean_typedef_declarator ')'
		{ $$ = $2; }
        | '(' clean_typedef_declarator ')' postfixing_abstract_declarator
		{ $2.set_abstract($4); $$ = $2; }
        ;

    /* The following have a redundant '(' placed immediately  to  the
    left of the TYPEDEF_NAME */

paren_typedef_declarator:
        paren_postfix_typedef_declarator
        | '*' '(' simple_paren_typedef_declarator ')' /* redundant paren */
		{ $3.set_abstract(pointer_to(basic())); $$ = $3; }
        | '*' type_qualifier_list
                '(' simple_paren_typedef_declarator ')' /* redundant paren */
		{ $4.set_abstract(pointer_to($2)); $$ = $4; }
        | '*' paren_typedef_declarator
		{ $2.set_abstract(pointer_to(basic())); $$ = $2; }
        | '*' type_qualifier_list paren_typedef_declarator
		{ $3.set_abstract(pointer_to($2)); $$ = $3; }
        ;

paren_postfix_typedef_declarator: /* redundant paren to left of tname*/
        '(' paren_typedef_declarator ')'
		{ $$ = $2; }
        | '(' simple_paren_typedef_declarator postfixing_abstract_declarator ')' /* redundant paren */
		{ $2.set_abstract($3); $$ = $2; }
        | '(' paren_typedef_declarator ')' postfixing_abstract_declarator
		{ $2.set_abstract($4); $$ = $2; }
        ;

simple_paren_typedef_declarator:
        TYPEDEF_NAME
        | '(' simple_paren_typedef_declarator ')'
		{ $$ = $2; }
        ;

attributed_identifier_declarator:
	  identifier_declarator
	| asm_or_attribute_list identifier_declarator asm_or_attribute_list
		{
			if ($1.qualified_unused()
			    || $2.qualified_unused()
			    || $3.qualified_unused())
				$2.get_token().set_ec_attribute(is_declared_unused);
			$$ = $2;
		}
	| asm_or_attribute_list identifier_declarator
		{
			if ($1.qualified_unused() || $2.qualified_unused())
				$2.get_token().set_ec_attribute(is_declared_unused);
			$$ = $2;
		}
	| identifier_declarator asm_or_attribute_list
		{
			if ($1.qualified_unused() || $2.qualified_unused())
				$1.get_token().set_ec_attribute(is_declared_unused);
			$$ = $1;
		}
	;

identifier_declarator:
        unary_identifier_declarator
        | paren_identifier_declarator
        ; /* Default rules */

unary_identifier_declarator:
	/* a[3] */
        postfix_identifier_declarator
	/* *a[3] */
        | '*' identifier_declarator
		{ $2.set_abstract(pointer_to(basic())); $$ = $2; }
	/* * const a[3] */
        | '*' type_qualifier_list identifier_declarator
		{ $3.set_abstract(pointer_to($2)); $$ = $3; }
        ;

postfix_identifier_declarator:
	/* int a[5]: declare a as array 5 of int */
        paren_identifier_declarator postfixing_abstract_declarator
		{
			$1.set_abstract($2);
			$$ = $1;
			if ($$.qualified_unused())
				$$.get_token().set_ec_attribute(is_declared_unused);
		}
		| '(' unary_identifier_declarator ')'
				{ $$ = $2; }
	/*  int (*a)[10]: declare a as pointer to array 10 of int */
        | '(' unary_identifier_declarator ')' postfixing_abstract_declarator
		{
			$2.set_abstract($4);
			$$ = $2;
			if ($$.qualified_unused())
				$$.get_token().set_ec_attribute(is_declared_unused);
		}
        ;

paren_identifier_declarator:
        IDENTIFIER
		{ $$ = $1; }
        | '(' paren_identifier_declarator ')'
		{ $$ = $2; }
        ;

old_function_declarator:
        postfix_old_function_declarator
        | '*' old_function_declarator
		{ $2.set_abstract(pointer_to(basic())); $$ = $2; }
        | '*' type_qualifier_list old_function_declarator
		{ $3.set_abstract(pointer_to($2)); $$ = $3; }
        ;

postfix_old_function_declarator:
        paren_identifier_declarator '(' { Block::enter(); } identifier_list { Block::param_exit(); } ')'
		{ $1.set_abstract(function_returning(basic(), $4.get_nparam())); $$ = $1; }
        | '(' old_function_declarator ')'
		{ $$ = $2; }
        | '(' old_function_declarator ')' postfixing_abstract_declarator
		{ $2.set_abstract($4); $$ = $2; }
        ;

abstract_declarator:
        unary_abstract_declarator
        | postfix_abstract_declarator
        | postfixing_abstract_declarator
        ; /* Default rules */

postfixing_abstract_declarator:
        array_abstract_declarator
        | '(' ')'
		{ $$ = function_returning(basic(), 0); }
        | '(' { Block::enter(); } parameter_type_list { Block::param_exit(); } ')'
		{ $$ = function_returning(basic(), $3.get_nparam()); }
        ;

constant_expression_opt:
	  /* EMPTY */
		{
			// ice_enter() called by array_abstract_declarator
			Pdtoken::ice_exit();
			$$ = basic();
		}
	| constant_expression
	;

array_qualifier:
	/*
	 * Could warn in the next three rules if we don't get a
	 * constant expression, but our compile-time evaluation
	 * isn't yet robust enough for this.
	 */
	  type_qualifier_list_opt constant_expression_opt
		{ $$ = $2; }
	| STATIC type_qualifier_list_opt constant_expression_opt
		{ $$ = $3; }
	| type_qualifier_list_opt STATIC constant_expression_opt
		{ $$ = $3; }
	| '*'
		{ $$ = basic(); }
	;

array_abstract_declarator:
        { Pdtoken::ice_enter(); } '[' array_qualifier ']'
		{
			Pdtoken::ice_exit();
			$$ = array_of(basic(), $3.get_value());
		}
        | array_abstract_declarator { Pdtoken::ice_enter(); }
	  '[' array_qualifier ']'
		{
			Pdtoken::ice_exit();
			$1.set_abstract(array_of(basic(), $4.get_value()));
			$$ = $1;
		}
        ;

unary_abstract_declarator:
        '*'
		{ $$ = pointer_to(basic()); }
        | '*' type_qualifier_list
		{ $$ = pointer_to($2); }
        | '*' abstract_declarator
		{ $2.set_abstract(pointer_to(basic())); $$ = $2; }
        | '*' type_qualifier_list abstract_declarator
		{ $3.set_abstract(pointer_to($2)); $$ = $3; }
        ;

postfix_abstract_declarator:
        '(' unary_abstract_declarator ')'
		{ $$ = $2; }
        | '(' postfix_abstract_declarator ')'
		{ $$ = $2; }
        | '(' postfixing_abstract_declarator ')'
		{ $$ = $2; }
        | '(' unary_abstract_declarator ')' postfixing_abstract_declarator
		{ $2.set_abstract($4); $$ = $2; }
        ;

/***************************** YACC RULES ***************************************/

file:
	YACC_COOKIE {
			if (DP())
				cout << "Parsing yacc code\n";
			parse_yacc_defs = true;
			yacc_typing = false;
			yacc_type.clear();
			yacc_identifier.clear();
		} yacc_body
	| translation_unit
	| /* Empty */
	;

yacc_body:
	yacc_defs YMARK
		{
			// typedef YYSTYPE int if not defined
			if (!yacc_typing)
				obj_define(Token(IDENTIFIER, "YYSTYPE"), yacc_stack = basic(b_int, s_none, c_typedef));
			// define YYSTYPE yacc_stack
			if (DP())
				cout << "Yacc stack is of type " << yacc_stack << "\n";
			// Set current function to yyparse()
			Id const *id = obj_lookup("yyparse");
			if (!id) {
				obj_define(Token(IDENTIFIER, "yyparse"), function_returning(basic(), 0));
				id = obj_lookup("yyparse");
			}
			FCall::set_current_fun(id);
		} yacc_rules
		{
			parse_yacc_defs = false;
			Call::unset_current_fun();
		} yacc_tail
	;

yacc_tail:
	/* Empty */
	| YMARK translation_unit
	;

yacc_defs:
	/* Empty */
	| yacc_defs yacc_def
	;

yacc_def:
	YSTART IDENTIFIER
		{
			make_yacc_identifier($2, y_nonterminal);
		}
	| UNION '{' { parse_yacc_defs = false; } member_declaration_list  '}'
		{
			Type ut = $4.clone();
			ut.set_storage_class(basic(b_abstract, s_none, c_typedef));
			obj_define(Token(IDENTIFIER, "YYSTYPE"), yacc_stack = ut);
			yacc_typing = true;
			parse_yacc_defs = true;
		}
	| YLCURL translation_unit YRCURL
	| yacc_terminal_decl yacc_terminal_list_declaration
	| YTYPE yacc_type_list_declaration
	| ';' /* Noop */
	;

yacc_terminal_decl:
	YTOKEN
	| YLEFT
	| YRIGHT
	| YNONASSOC
	;

yacc_tag:
	/* Empty: union tag is optional */
		{ $$ = basic(b_undeclared); }
	| '<' IDENTIFIER '>'
			{
				if (!yacc_typing)
					/*
					 * @error
					 * The yacc $<tag>n syntax was used
					 * to specify an element of the %union
					 * but no union was defined.
					 */
					Error::error(E_ERR, "explicit element tag without no %union in effect");
				Id const *id = yacc_stack.member($2.get_name());
				if (id) {
					if (DP())
						cout << ". returns " << id->get_type() << "\n";
					csassert(id->get_name() == $2.get_name());
					Token::unify(id->get_token(), $2.get_token());
				} else {
					/*
					 * @error
					 * The yacc %union
					 * does not have as a member the
					 * identifier appearing on the
					 * element's tag
					 */
					Error::error(E_ERR, "unkown %union element tag " + $2.get_name());
				}
				$$ = $2;
			}
	;

yacc_terminal_list_declaration:
	/*
	 * As in: %token <Tree> expr, statement
	 *               tag    yacc_name_number
	 */
	yacc_tag yacc_name_number
		{
			yacc_type_define($2, $1, y_terminal);
			Pdtoken::create_undefined_macro($2.get_token());
			$$ = $1;
		}
	| yacc_terminal_list_declaration comma_opt yacc_name_number
		{
			yacc_type_define($3, $1, y_terminal);
			Pdtoken::create_undefined_macro($3.get_token());
			$$ = $1;
		}
	;

yacc_type_list_declaration:
	/*
	 * As in: %type <Tree> expr, statement
	 *              tag    yacc_name_number
	 */
	yacc_tag yacc_name_number
		{
			yacc_type_define($2, $1, y_nonterminal);
			$$ = $1;
		}
	| yacc_type_list_declaration comma_opt yacc_name_number
		{
			yacc_type_define($3, $1, y_nonterminal);
			$$ = $1;
		}
	;

yacc_name_number:
	yacc_name
	| yacc_name INT_CONST
	;

yacc_name:
	IDENTIFIER
		{
			make_yacc_identifier($1, y_any);
			$$ = $1;
		}
	| CHAR_LITERAL
	;

/* rules section */

yacc_rules:
	yacc_rule
	| yacc_rules yacc_rule
	;

/*
 * Older yacc versions apparently accepted rules without terminating ;
 * We deviate from Johnson's yacc grammar published as appendix B
 * of his "Yacc---Yet Another Compiler-Compiler" report and only
 * accept the modern syntax
 */

yacc_rule:
	/* yacc_dollar[0] is the name for $$ */
	IDENTIFIER ':' { yacc_dollar.clear(); yacc_dollar.push_back($1.get_name()); } yacc_rule_body_list ';'
		{
			make_yacc_identifier($1, y_nonterminal);
		}
	;

yacc_rule_body_list:
	yacc_rule_body
	| yacc_rule_body_list '|' yacc_rule_body
	;

yacc_rule_body:
	{
		/* Erase $1 ... $n */
		yacc_dollar.erase(yacc_dollar.begin() + 1, yacc_dollar.end());
	}
	yacc_id_action_list yacc_prec
	;

yacc_id_action_list:
	/* Empty */
	| yacc_id_action_list yacc_name
		{ yacc_dollar.push_back($2.get_name()); }
        | yacc_id_action_list { parse_yacc_defs = false; } equal_opt compound_statement
		{
			parse_yacc_defs = true;
			yacc_dollar.push_back("_ACTION_");
		}
	;

yacc_prec:
	/* Empty */
	| YPREC yacc_name
	| YPREC yacc_name { parse_yacc_defs = false; } equal_opt compound_statement
		{ parse_yacc_defs = true; }
	;

yacc_variable:
	'$' '$'
		{
			YaccTypeMap::const_iterator i = yacc_type.find(yacc_dollar[0]);
			if (i == yacc_type.end())
				$$ = basic(b_int);
			else
				$$ = (*i).second;
		}
	| '$' INT_CONST
		{
			char *endptr;
			int val = strtol($2.get_name().c_str(), &endptr, 0);
			if ((unsigned)val >= yacc_dollar.size()) {
				/*
				 * @error
				 * The number used in a $n yacc variable was greater than the
				 * number of identifiers and actions on the action's left side
				 */
				Error::error(E_ERR, "yacc $value out of range");
				$$ = basic(b_int);
			}
			YaccTypeMap::const_iterator i = yacc_type.find(yacc_dollar[val]);
			if (i == yacc_type.end())
				$$ = basic(b_int);
			else
				$$ = (*i).second;
			if (DP())
				cout << "yacc type of $" << val << " which is " <<
				yacc_dollar[val] << " resolves to " << $$ << "\n";
		}
	| '$' '-' INT_CONST
		{ $$ = basic(b_int); }
	| '$' '<' IDENTIFIER '>' yacc_variable_suffix
		{ $$ = $3; }
	;

yacc_variable_suffix:
	'$'
	| INT_CONST
	| '-' INT_CONST
	;

equal_opt:
	/* Empty */
	| '='
	;

%%
/* ----end of grammar----*/

    /* Copyright (C) 1989,1990 James A. Roskind, All rights reserved.
    This grammar was developed  and  written  by  James  A.  Roskind.
    Copying  of  this  grammar  description, as a whole, is permitted
    providing this notice is intact and applicable  in  all  complete
    copies.   Translations as a whole to other parser generator input
    languages  (or  grammar  description  languages)   is   permitted
    provided  that  this  notice is intact and applicable in all such
    copies,  along  with  a  disclaimer  that  the  contents  are   a
    translation.   The reproduction of derived text, such as modified
    versions of this grammar, or the output of parser generators,  is
    permitted,  provided  the  resulting  work includes the copyright
    notice "Portions Copyright (c)  1989,  1990  James  A.  Roskind".
    Derived products, such as compilers, translators, browsers, etc.,
    that  use  this  grammar,  must also provide the notice "Portions
    Copyright  (c)  1989,  1990  James  A.  Roskind"  in   a   manner
    appropriate  to  the  utility,  and in keeping with copyright law
    (e.g.: EITHER displayed when first invoked/executed; OR displayed
    continuously on display terminal; OR via placement in the  object
    code  in  form  readable in a printout, with or near the title of
    the work, or at the end of the file).  No royalties, licenses  or
    commissions  of  any  kind are required to copy this grammar, its
    translations, or derivative products, when the copies are made in
    compliance with this notice. Persons or corporations that do make
    copies in compliance with this notice may charge  whatever  price
    is  agreeable  to  a  buyer, for such copies or derivative works.
    THIS GRAMMAR IS PROVIDED ``AS IS'' AND  WITHOUT  ANY  EXPRESS  OR
    IMPLIED  WARRANTIES,  INCLUDING,  WITHOUT LIMITATION, THE IMPLIED
    WARRANTIES  OF  MERCHANTABILITY  AND  FITNESS  FOR  A  PARTICULAR
    PURPOSE.

    James A. Roskind
    Independent Consultant
    516 Latania Palm Drive
    Indialantic FL, 32903
    (407)729-4348
    jar@ileaf.com


    ---end of copyright notice---


This file is a companion file to a C++ grammar description file.

*/


/* FILENAME: C.Y */

/*  This  is a grammar file for the dpANSI C language.  This file was
last modified by J. Roskind on 3/7/90. Version 1.00 */


/* ACKNOWLEDGMENT:

Without the effort expended by the ANSI C standardizing committee,  I
would  have been lost.  Although the ANSI C standard does not include
a fully disambiguated syntax description, the committee has at  least
provided most of the disambiguating rules in narratives.

Several  reviewers  have also recently critiqued this grammar, and/or
assisted in discussions during it's preparation.  These reviewers are
certainly not responsible for the errors I have committed  here,  but
they  are responsible for allowing me to provide fewer errors.  These
colleagues include: Bruce Blodgett, and Mark Langley. */
