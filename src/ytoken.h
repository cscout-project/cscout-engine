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
 * Token definitions for btyacc
 * Kept here to keep them in sync between the preprocessor expression
 * and the main grammar.
 *
 */


/* Define terminal tokens */

/* keywords */
%token AUTO            DOUBLE          INT             STRUCT
%token BREAK           ELSE            LONG            SWITCH
%token CASE            ENUM            REGISTER        TYPEDEF
%token CHAR            EXTERN          RETURN          UNION
%token TCONST          FLOAT           SHORT           UNSIGNED
%token CONTINUE        FOR             SIGNED          TVOID
%token DEFAULT         GOTO            SIZEOF          VOLATILE
%token DO              IF              STATIC          WHILE
%token RESTRICT        INLINE	       BOOL	       THREAD_LOCAL
%token GENERIC	       COMPLEX         IMAGINARY       SIMD
%token NORETURN        STATIC_ASSERT   ATOMIC          CONSTEXPR       NULLPTR

/* ANSI Grammar suggestions */
%token IDENTIFIER         STRING_LITERAL
%token FLOAT_CONST        INT_CONST


/*
 * New Lexical element, whereas ANSI suggested non-terminal
 * Lexer will tell part of the difference between this and
 * an  identifier!   An  identifier  that is CURRENTLY in scope as a
 * typedef name is provided to the parser as a TYPEDEF_NAME.
 */
%token TYPEDEF_NAME

/* Multi-Character operators */
%token  PTR_OP                       /*    ->                              */
%token  INC_OP DEC_OP                /*    ++      --                      */
%token  LEFT_OP RIGHT_OP             /*    <<      >>                      */
%token  LE_OP GE_OP EQ_OP NE_OP      /*    <=      >=      ==      !=      */
%token  AND_OP OR_OP                 /*    &&      ||                      */
%token  ELLIPSIS                     /*    ...                             */

/* modifying assignment operators */
%token MUL_ASSIGN  DIV_ASSIGN    MOD_ASSIGN   /*   *=      /=      %=      */
%token ADD_ASSIGN  SUB_ASSIGN                 /*   +=      -=              */
%token LEFT_ASSIGN RIGHT_ASSIGN               /*   <<=     >>=             */
%token AND_ASSIGN  XOR_ASSIGN    OR_ASSIGN    /*   &=      ^=      |=      */


/* Tokens needed by the pre-processor */
%token CPP_CONCAT SPACE ABSFNAME PATHFNAME CHAR_LITERAL PP_NUMBER

/* gcc extensions */
/* __asm__ directive */
%token GNUC_ASM
/* __auto_type specifier */
%token AUTO_TYPE
/* __typeof operators */
%token TYPEOF
/* __typeof_unqual__ operator */
%token TYPEOF_UNQUAL
/* __alignof operator */
%token ALIGNOF
/* _Alignas alignment specifier (C11) */
%token ALIGNAS
/* __label__ declaration */
%token LABEL
/* __builtin_choose_expr */
%token CHOOSE_EXPR

/* The result of a __attribute__(__unused__) sequence */
%token UNUSED
/* The __attribute__ token */
%token ATTRIBUTE

/* MSC _asm directive */
%token MSC_ASM

/* Microsoft __try __except __finally extensions */
%token TRY        FINALLY	EXCEPT		LEAVE

/* Yacc tokens */
%token YMARK	YLCURL	YRCURL			/* %%	%{	%}	*/
%token YLEFT YRIGHT YNONASSOC YTOKEN YPREC YTYPE YSTART YBAD
/*
 * Magic cookie we push at the beginning of yacc files to turn the parser
 * into yacc mode
 */
%token YACC_COOKIE
