/*
 * (C) Copyright 2001-2026 Diomidis Spinellis
 *
 * You may only use this code if you agree to the terms of the CScout
 * Source Code License agreement (see License.txt).
 * If you do not agree to the terms, do not use the code.
 *
 * For documentation read the corresponding .h file
 *
 */

#include <iostream>
#include <map>
#include <string>
#include <deque>
#include <vector>
#include <stack>
#include <iterator>
#include <fstream>
#include <list>
#include <set>
#include <algorithm>
#include <functional>		// ptr_fun
#include <cctype>		// isspace

#include "cpp.h"
#include "debug.h"
#include "debug_out.h"
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
#include "tchar.h"
#include "ctoken.h"
#include "type.h"
#include "call.h"
#include "mcall.h"
#include "metrics.h"

/*
 * Register a call associated with a given macro substituting
 * the specified token.
 * This gives registers a call from the macro that produced the
 * token (caller) to the macro substituting the token (callee).
 * If the caller is NULL then a call from the enclosing C
 * function is registered.
 */
static void
register_call(const Macro *substitutor, const Ptoken &substituted)
{
	if (DP())
		cout << "register_call substitutor: " << *substitutor
		    << "substituted: " << substituted;

	const Macro *caller = substituted.get_producer();
	if (caller && caller->get_is_function())
		// Macro to macro call
		Call::register_call(caller->get_mcall(), substitutor->get_mcall());
	else
		// Function to macro call
		Call::register_call(substitutor->get_mcall());
}


/*
 * Return a macro argument token from tokens, which can be PtokenSequence
 * or dequePtoken.
 * Used by gather_args and gather_arg.
 * If get_more is true when tokens is exhausted read using pdtoken::getnext_noexpand
 * (see explanation on that method's comment for why we use pdtoken, rather than pltoken)
 * Leave in tokens the first token not gathered.
 * If want_space is true return spaces, otherwise discard them
 * collector is the macro collecting tokens for its substitution.
 */
template <typename TokenContainer>
static Ptoken
arg_token(TokenContainer& tokens, bool get_more, bool want_space, const Macro *collector)
{
	if (want_space) {
		if (!tokens.empty()) {
			Ptoken r(tokens.front());
			register_call(collector, r);
			tokens.pop_front();
			return (r);
		} else if (get_more) {
			Pdtoken t;
			t.getnext_noexpand();
			return (t);
		} else
			return Ptoken(EOF, "");
	} else {
		while (!tokens.empty() && tokens.front().is_space())
			tokens.pop_front();
		if (!tokens.empty()) {
			Ptoken r(tokens.front());
			register_call(collector, r);
			tokens.pop_front();
			return (r);
		} else if (get_more) {
			Pdtoken t;
			do {
				t.getnext_noexpand_nospc();
			} while (t.get_code() != EOF && t.is_space());
			return (t);
		} else
			return Ptoken(EOF, "");
	}
}

/*
 * Get the macro arguments specified in formal_args, initiallly by
 * removing them from tokens, then, if get_more is true, from pdtoken.getnext_noexpand.
 * The opening bracket has already been gathered.
 * Build the map from formal name to argument value args.
 * collector is the macro collecting tokens for its substitution.
 * Return in close the closing bracket token (used for its hideset)
 * Argument values are whitespace-trimmed to prevent spaces making
 * empty arguments falsely appear as non-empty. (Macro substitution
 * tests emptiness in numerous places.)
 * Return true if ok, false on error.
 */
static bool
gather_args(const Macro *collector, PtokenSequence& tokens, mapArgval& args, bool get_more, Ptoken &close)
{
	const dequePtoken& formal_args(collector->get_formal_args());
	Ptoken t;
	dequePtoken::const_iterator i;
	for (i = formal_args.begin(); i != formal_args.end(); i++) {
		PtokenSequence& v = args[i->get_val()];
		char terminate;
		if (i + 1 == formal_args.end())
			terminate = ')';
		else if (collector->get_is_vararg() && i + 2 == formal_args.end())
			terminate = '.';	// Vararg last argument is optional; terminate with ) or ,
		else
			terminate = ',';
		// Get a single argument
		bool want_space = false;	// To trim leading spaces
		int bracket = 0;
		for (;;) {
			t = arg_token(tokens, get_more, want_space, collector);
			want_space = true;
			if (bracket == 0 && (
				(terminate == '.' && (t.get_code() == ',' || t.get_code() == ')')) ||
				(terminate != '.' && t.get_code() == terminate)))
					break;
			switch (t.get_code()) {
			case '(':
				bracket++;
				break;
			case ')':
				bracket--;
				break;
			case EOF:
				/*
				 * @error
				 * The end of file was reached while
				 * gathering a macro's arguments
				 */
				Error::error(E_ERR, "macro [" + collector->get_name() + "]: EOF while reading function macro arguments");
				return (false);
			}
			v.push_back(t);
		}
		// Remove trailing whitespace.
		while (!v.empty() && v.back().is_space())
			v.pop_back();
		if (DP()) cout << "Gather args returns: " << v << "\n";
		// Check if varargs last optional argument was not supplied
		if (terminate == '.' && t.get_code() == ')') {
			i++;
			// Instantiate argument with an empty value list
			args[i->get_val()];
			break;
		}
		close = t;
	}
	if (formal_args.size() == 0) {
		t = arg_token(tokens, get_more, false, collector);
		if (t.get_code() != ')') {
			/*
			 * @error
			 * The arguments to a function-like macro did
			 * not terminate with a closing bracket
			 */
			Error::error(E_ERR, "macro [" + collector->get_name() + "]: close bracket expected for function-like macro");
			return (false);
		}
	}
	return (true);
}


// Return s with all \ and " characters \ escaped
static string
escape(const string& s)
{
	string r;

	for (string::const_iterator i = s.begin(); i != s.end(); i++)
		switch (*i) {
		case '\\':
		case '"':
			r += '\\';
			[[fallthrough]];
		default:
			r += *i;
		}
	return (r);
}

/*
 * Convert a list of tokens into a string as specified by the # operator
 * Multiple spaces are converted to a single space, \ and " are
 * escaped.
 * Macro is the invoking macro.  It is used for registering calls.
 */
static Ptoken
stringize(const PtokenSequence& ts)
{
	string res;
	PtokenSequence::const_iterator pi;
	bool seen_space = true;		// To delete leading spaces

	for (pi = ts.begin(); pi != ts.end(); pi++) {
		pi->set_cpp_str_val();
		switch ((*pi).get_code()) {
		case '\n':
		case SPACE:
			if (seen_space)
				continue;
			else
				seen_space = true;
			res += ' ';
			break;
		case STRING_LITERAL:
			seen_space = false;
			res += "\\\"" + escape((*pi).get_val()) + "\\\"";
			break;
		case CHAR_LITERAL:
			seen_space = false;
			res += '\'' + escape((*pi).get_val()) + '\'';
			break;
		default:
			seen_space = false;
			res += (*pi).get_val();
			break;
		}
	}
	// Remove trailing spaces
	res.erase(find_if(res.rbegin(), res.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), res.end());
	return (Ptoken(STRING_LITERAL, res));
}

/*
 * Remove from tokens and return the elements comprising the arguments to a
 * function macro, such as __VA_OPT__.
 * collector is the macro collecting tokens for its substitution.
 */
static PtokenSequence
gather_function_arg(dequePtoken& tokens, const Macro *collector)
{
	PtokenSequence r;
	int bracket_nesting = 0;

	for (;;) {
		Ptoken t(arg_token(tokens, false, true, collector));
		switch (t.get_code()) {
		case '(':
			bracket_nesting++;
			// Don't save opening bracket
			if (bracket_nesting == 1)
				continue;
			break;
		case ')':
			bracket_nesting--;
			// Don't save closing bracket; return contents
			if (bracket_nesting == 0) {
				if (DP())
					cout << "Gather returns " << r << "\n";
				return r;
			}
			break;
		case EOF:
			/*
			 * @error
			 * End of file encountered while scanning
			 * a function-like macro's argument.
			 */
			Error::error(E_ERR, "EOF in function-like macro");
			return (r);
		}
		r.push_back(t);
	}
}

/*
 * Remove and return a preprocessor operator's operand, keeping it out of the
 * ordinary macro-expansion pass.  defined also permits an unparenthesized
 * identifier; header availability operators require parentheses.
 */
PtokenSequence
gather_operand(PtokenSequence& tokens, PtokenSequence::iterator position,
    bool requires_parenthesis)
{
	PtokenSequence r;
	int nesting = 0;

	// Preserve whitespace between the operator and its operand.
	while (position != tokens.end() && position->get_code() == SPACE) {
		auto current = position++;
		r.splice(r.end(), tokens, current);
	}

	if (!requires_parenthesis) {
		if (position == tokens.end())
			return r;
		if (position->get_code() != '(') {
			auto current = position++;
			r.splice(r.end(), tokens, current);
			return r;
		}
	}

	if (position == tokens.end() || position->get_code() != '(') {
		/*
		 * @error
		 * A parenthesized preprocessor operator was not followed by an
		 * opening bracket.
		 */
		Error::error(E_ERR, "Missing open bracket after preprocessor operator");
		return r;
	}

	// Gather the complete operand, allowing parentheses produced by macros.
	do {
		Ptoken t(*position);
		auto current = position++;
		r.splice(r.end(), tokens, current);
		if (t.get_code() == '(')
			nesting++;
		else if (t.get_code() == ')')
			nesting--;
	} while (position != tokens.end() && nesting != 0);

	if (nesting != 0) {
		/*
		 * @error
		 * The end of a preprocessor expression was reached before the
		 * closing bracket of an operator operand.
		 */
		Error::error(E_ERR, "Missing close bracket in preprocessor operator");
	}

	// Return the protected tokens for later operand-specific expansion.
	return r;
}

// Return an arg iterator if token is a formal argument
// otherwise end()
static inline mapArgval::const_iterator
find_formal_argument(const mapArgval &args, const Ptoken &t)
{
	if (DP())
		cout << "find formal argument: " << t << "\n";
	if (t.get_code() != IDENTIFIER)
		return args.end();
	else
		return args.find(t.get_val());
}

static inline bool
space_eq(Ptoken& a, Ptoken& b)
{
	return a.is_space() && b.is_space();
}

// True if two macro definitions are the same
bool
operator ==(const Macro& a, const Macro& b)
{
	if (a.is_function != b.is_function || a.formal_args != b.formal_args)
		return false;

	// Remove consecutive spaces
	dequePtoken va(a.value);
	dequePtoken vb(b.value);
	va.erase(unique(va.begin(), va.end(), space_eq), va.end());
	vb.erase(unique(vb.begin(), vb.end(), space_eq), vb.end());
	return (va == vb);
}

// Remove trailing whitespace
void
Macro::value_rtrim()
{
	// Took me three hours to arrive at
	value.erase(find_if(value.rbegin(), value.rend(), [&](const Ptoken& token) { return !token.is_space(); }).base(), value.end());
}

ostream&
operator<<(ostream& o,const Macro &m)
{
	o << nest_begin("Macro: {")
		<< nest("name") << m.name_token.get_val() << '\n';
	if (m.is_function) {
		o << nest("function") << "true" << '\n';
		o << nest("args");
		for (dequePtoken::const_iterator i = m.formal_args.begin(); i != m.formal_args.end(); i++)
			o << (*i).get_val() << ", ";
		o << '\n';
	}

	o << nest("body");
	for (dequePtoken::const_iterator i = m.value.begin(); i != m.value.end(); i++)
		o << (*i).get_val();
	o << '\n';

	o << nest_end("}");
	return (o);
}

// Update the map to include the macro's body refering to the macro
void
Macro::register_macro_body(mapMacroBody &map) const
{
	for (dequePtoken::const_iterator i = value.begin(); i != value.end(); i++)
		for (dequeTpart::const_iterator j = i->get_parts_begin(); j != i->get_parts_end(); j++)
			map[j->get_tokid()] = this->mcall;
}

// Constructor
Macro::Macro( const Ptoken& name, bool id, bool isfun, bool isimmutable) :
	name_token(name),
	is_function(isfun),
	is_immutable(isimmutable),
	is_vararg(false),
	is_defined(id)
{
	if (isfun) {
		Token uname(name.unique());	// Take care of identical files
		mcall = dynamic_cast<MCall *>(Call::get_call(name));
		if (!mcall) {
			if (DP())
				cout << "Creating new macro call\n";
			mcall = new MCall(name, name.get_name());
		}
		if (DP())
			cout << "Mcall is " << mcall << "\n";
	} else
		mcall = NULL;	// To void nasty surprises
}

static PtokenSequence subst(const Macro &m, const mapArgval &args, HideSet hs, bool skip_defined, Macro::MacroType macro_type, Macro::CalledContext context);
static PtokenSequence glue(PtokenSequence ls, PtokenSequence rs);
static bool fill_in(PtokenSequence &ts, bool get_more, PtokenSequence &removed);

/*
 * Cached macro argument expansions.
 * The cache is used to match GCC documentation:
 * "Macro arguments are completely macro-expanded before they are
 * substituted into a macro body, unless they are stringized or pasted
 * with other tokens."
 * This caching is required, so that __COUNTER__ works as expected.
 */
struct ExpandedArg {
	PtokenSequence value;	// Once-expanded argument tokens to reuse
	int intoken = 0;	// Captured em_nmacrointoken delta
	int outtoken = 0;	// Captured em_nmacroouttoken delta
};

// Active expanded argument metric captures, including nested expansions.
static vector<ExpandedArg *> macro_expansion_metric_captures;

// Tally macro expansion metrics and mirror them into active captures.
static void
add_macro_expand_metric(int metric, int value, int ExpandedArg::*captured)
{
	Metrics::add_pre_cpp_metric(metric, value);
	for (auto capture : macro_expansion_metric_captures)
		capture->*captured += value;
}

struct MacroExpansionMetricCapture {
	ExpandedArg arg;	// Expanded argument and its metric deltas

	MacroExpansionMetricCapture() {
		macro_expansion_metric_captures.push_back(&arg);
	}

	~MacroExpansionMetricCapture() {
		macro_expansion_metric_captures.pop_back();
	}
};

/*
 * Expand a token sequence
 * This is an implementation of Dave Prosser's algorithm, listed in
 * X3J11/86-196 and in https://www.spinellis.gr/blog/20060626/.
 * with recursion converted to iteration.
 * (Commit 5b08cdebf7778e3f10c0faa3a39dc5a46991ea00 contains
 * a recursive version.)
 * This function goes through sequence ts token by token and expands macros.
 * If token_source is get_more, then more tokens can be fetched.
 * If operator_handling is skip, preprocessor operator operands are protected
 * from the ordinary macro expansion pass.
 * If context denotes preprocessor then is_cpp_const attribute is set
 * for identifiers.
 *
 * # Call handling
 * Tokens are recursively substituted left to right.  Therefore,
 * neither expand() nor subst() can be used directly to create the caller
 * callee relationships via a passed caller argument.  Instead, associate
 * the producing macro with tokens, and use that as the caller.
 *
 * When invoking subst, the macro M being substituted is the callee.
 * So, in a function-like macro context, input sequence tokens
 * generate calls from their producer to M.
 * The output sequence tokens are tagged as produced by M.
 *
 * When substituting an object-like macro, the input token's producer
 * is propagated.
 */
PtokenSequence
macro_expand(PtokenSequence ts,
    Macro::TokenSourceOption token_source,
    Macro::OperatorHandling operator_handling,
    Macro::CalledContext context)
{
	PtokenSequence r;	// Return value
	set<Macro> expanded_macros; // For adding attributes
	auto ts_size = ts.size();

	if (DP()) cout << "macro_expand: expanding sequence: " << ts << endl;
	while (!ts.empty()) {
		const Ptoken head(ts.front());
		ts.pop_front();

		if (head.get_code() != IDENTIFIER) {
			// Only attempt to expand identifiers (not e.g. string literals)
			r.push_back(head);
			continue;
		}

		if (operator_handling == Macro::OperatorHandling::skip
		    && head.get_code() == IDENTIFIER
		    && (head.get_val() == "defined"
		        || head.get_val() == "__has_include"
		        || head.get_val() == "__has_include_next")) {
			PtokenSequence operand(gather_operand(ts, ts.begin(),
			    head.get_val() != "defined"));
			r.push_back(head);
			r.splice(r.end(), operand);
			continue;
		}

		const string name = head.get_val();

		if (name == "__COUNTER__") {
			static int counter = 0;
			Ptoken str(PP_NUMBER, to_string(counter));
			counter += 1;
			r.push_back(str);
			continue;
		}

		if (name == "__LINE__") {
			Ptoken str(PP_NUMBER, to_string(Fchar::get_line_num()));
			r.push_back(str);
			continue;
		}

		mapMacro::const_iterator mi(Pdtoken::macros_find(name));
		if (!Pdtoken::macro_is_defined(mi)) {
			// Nothing to do if the identifier is not a macro
			r.push_back(head);
			continue;
		}

		// Mark the macro as used as a preprocessor constant
		if (context == Macro::CalledContext::process_include
		    || context == Macro::CalledContext::process_if)
			head.set_ec_attribute(is_cpp_const);

		const Macro& m = mi->second;
		if (head.hideset_contains(m.get_name_token())) {
			// Skip the head token if it is in the hideset
			if (DP()) cout << "macro_expand: skipping (head is in HS)" << endl;
			r.push_back(head);
			continue;
		}

		if (DP())
			cout << "macro_expand: replacing for \""
				<< name << "\" "
				<< nest_begin("tokens: [")
				<< ts
				<< nest_end("]");
		expanded_macros.insert(m);
		PtokenSequence removed_spaces;
		if (!m.is_function) {
			// Object-like macro
			if (DP())
				cout << "macro_expand: expanding object-like " << m;
			Token::unify((*mi).second.name_token, head);
			HideSet hs(head.get_hideset());
			hs.insert(m.get_name_token());
			PtokenSequence s(subst(m, mapArgval(), hs, operator_handling == Macro::OperatorHandling::skip, Macro::MacroType::object_like, context));
			ts.splice(ts.begin(), s);
		} else if (fill_in(ts, token_source == Macro::TokenSourceOption::get_more, removed_spaces) && ts.front().get_code() == '(') {
			// Application of a function-like macro
			Token::unify((*mi).second.name_token, head);
			register_call(&m, head);
			mapArgval args;			// Map from formal name to value

			if (DP())
				cout << "macro_expand: expanding function-like " << m;
			ts.pop_front();
			Ptoken close;
			if (!gather_args(&m, ts, args, token_source == Macro::TokenSourceOption::get_more, close))
				continue;	// Attempt to bail-out on error
			HideSet hs;
			set_intersection(head.get_hideset().begin(), head.get_hideset().end(),
				close.get_hideset().begin(), close.get_hideset().end(),
				inserter(hs, hs.begin()));
			hs.insert(m.get_name_token());
			PtokenSequence s(subst(m, args, hs, operator_handling == Macro::OperatorHandling::skip, Macro::MacroType::function_like, context));
			ts.splice(ts.begin(), s);
		} else {
			// Function-like macro name lacking a (
			if (DP()) cout << "macro_expand: splicing: [" << removed_spaces << ']' << endl;
			ts.splice(ts.begin(), removed_spaces);
			r.push_back(head);
		}
	}
	if (DP()) cout << "macro_expand: result: " << r << endl;

	add_macro_expand_metric(Metrics::em_nmacrointoken, ts_size, &ExpandedArg::intoken);
	add_macro_expand_metric(Metrics::em_nmacroouttoken, r.size(), &ExpandedArg::outtoken);

	/*
	 * After all macros have been expanded, mark the expanded macros
	 * on whether the value can or cannot be a C compile-time constant.
	 * Over a heuristic check made by examining the macro's definition,
	 * the expansion method has the advantage of resolving constants
	 * defined as other macros, (e.g. FLAG_MASK) *provided* the macro is
	 * actually used.
	 */
	if (context == Macro::CalledContext::process_c) {
		enum e_attribute attr = is_c_const(r)
			? is_exp_c_const : is_exp_not_c_const;
		for (const auto &m : expanded_macros)
			m.get_name_token().set_ec_attribute(attr);
	}

	return (r);
}

/*
 * Return the position of the first non-space token in the range [pos, end)
 * Return end, if no such token is found
 */
static inline dequePtoken::iterator
find_nonspace(dequePtoken::iterator pos, dequePtoken::iterator end)
{
	for (; pos != end; pos++)
		if (!pos->is_space())
			return (pos);
	return (end);
}

/*
 * Substitute the arguments args (may be empty) in the body of of macro m,
 * also handling stringization and concatenation returning the macro's
 * replacement text.
 * This is Prosser's subst() function.
 * Result is created in the output sequence os and finally has the specified
 * hide set and producer macro added to it, before getting returned.
 */
static PtokenSequence
subst(const Macro &m, const mapArgval &args, HideSet hs, bool skip_defined, Macro::MacroType macro_type, Macro::CalledContext context)
{
	dequePtoken is(m.get_value());// Input sequence
	PtokenSequence os;	// Output sequence
	map<string, ExpandedArg> expanded_args;	// Fully-expanded ordinary arguments
	static const Ptoken VA_ARGS(IDENTIFIER, "__VA_ARGS__");


	while (!is.empty()) {
		if (DP())
			cout << "subst: "
			    << nest_begin("IS: {") << is << nest_end("}")
			    << nest_begin("OS: {") << os << nest_end("}");
		const Ptoken head(is.front());
		is.pop_front();		// is is now the tail
		dequePtoken::iterator ti, ti2;
		mapArgval::const_iterator ai;
		switch (head.get_code()) {
		case '#':		// Stringizing operator
			ti = find_nonspace(is.begin(), is.end());
			if (ti != is.end() && (ai = find_formal_argument(args, *ti)) != args.end()) {
				is.erase(is.begin(), ++ti);
				Ptoken str(stringize(ai->second));
				str.set_producer(&m);
				os.push_back(str);
				continue;
			}
			break;
		case CPP_CONCAT:
			ti = find_nonspace(is.begin(), is.end());
			if (ti != is.end()) {
				if ((ai = find_formal_argument(args, *ti)) != args.end()) {
					is.erase(is.begin(), ++ti);
					if (ai->second.size() != 0)	// Only if actuals can be empty
						os = glue(os, ai->second);
				} else {
					PtokenSequence t(ti, ti + 1);
					is.erase(is.begin(), ++ti);
					os = glue(os, t);
				}
				continue;
			}
			break;
		case IDENTIFIER:
			if (head.get_val() == "__VA_OPT__") {
				/*
				 * Implement the __VA_OPT__ function macro.
				 * This expands to its argument if the variadic
				 * argument has any tokens, otherwise it expands
				 * to empty.
				 */
				if (!m.get_is_vararg()) {
					/*
					 * @error
					 * The macro body of a non variadic
					 * function-like macro contains a call
					 * to the __VA_OPT__ function macro.
					 */
					Error::error(E_ERR, "Call to __VA_OPT__ from a non variadic macro.");
					break;
				}
				PtokenSequence opt(gather_function_arg(is, &m));

				if ((ai = find_formal_argument(args, VA_ARGS)) != args.end() && ai->second.size() != 0)
					os.splice(os.end(), opt);
				continue;
			}
			[[fallthrough]];
		default:
			ti = find_nonspace(is.begin(), is.end());
			if (ti != is.end() && ti->get_code() == CPP_CONCAT) {
				/*
				 * Implement the following gcc extension:
				 * "`##' before a rest argument that is empty
				 * discards the preceding "," token from
				 *  the macro definition.
				 * Otherwise, break to process a non-formal
				 * argument in the default way
				 */
				if (head.get_code() == ','
				    && m.get_is_vararg()) {
					ti2 = find_nonspace(ti + 1, is.end());
					if (ti2 != is.end() && (ai = find_formal_argument(args, *ti2)) != args.end() && ai->second.size() == 0) {
						// All conditions satisfied; discard elements:
						// <non-formal> <##> <empty-formal>
						is.erase(is.begin(), ++ti2);
						continue;
					}
				}
				if ((ai = find_formal_argument(args, head)) == args.end())
					break;	// Non-formal arguments don't deserve special treatment
				// Paste but not expand LHS, RHS
				if (ai->second.size() == 0) {	// Only if actuals can be empty
					is.erase(is.begin(), ++ti);	// Erase including ##
					ti = find_nonspace(is.begin(), is.end());
					if (ti != is.end() && (ai = find_formal_argument(args, *ti)) != args.end()) {
						is.erase(is.begin(), ++ti);	// Erase the ## RHS
						PtokenSequence actual(ai->second);
						os.splice(os.end(), actual);
					}
				} else {
					is.erase(is.begin(), ti);	// Erase up to ##
					PtokenSequence actual(ai->second);
					os.splice(os.end(), actual);
				}
				continue;
			} // end of ## before a rest argument

			if ((ai = find_formal_argument(args, head)) == args.end())
				break;

			// Otherwise use the formal argument's cached value.
			map<string, ExpandedArg>::const_iterator pi = expanded_args.find(ai->first);
			if (pi == expanded_args.end()) {
				MacroExpansionMetricCapture capture;
				PtokenSequence expanded(macro_expand(ai->second,  Macro::TokenSourceOption::use_supplied, skip_defined ? Macro::OperatorHandling::skip : Macro::OperatorHandling::process, context));
				capture.arg.value = expanded;
				pi = expanded_args.emplace(ai->first, capture.arg).first;
			} else {
				add_macro_expand_metric(Metrics::em_nmacrointoken, pi->second.intoken, &ExpandedArg::intoken);
				add_macro_expand_metric(Metrics::em_nmacroouttoken, pi->second.outtoken, &ExpandedArg::outtoken);
			}
			PtokenSequence expanded(pi->second.value);
			os.splice(os.end(), expanded);
			continue;
		}
		os.push_back(head);
	}

	// Add hs to the hide set of every element of os
	for (PtokenSequence::iterator oi = os.begin(); oi != os.end(); ++oi)
		oi->hideset_insert(hs.begin(), hs.end());
	if (DP()) cout << "subst: after adding hs: "
	    << nest_begin("os: ") << os << nest_end("}");

	// Set all output tokens at produced by the function-like macro.
	if (macro_type == Macro::MacroType::function_like) {
		if (DP())
			cout << "Set producer " << m << "for " << os;
		for (Ptoken &t : os)
			t.set_producer(&m);
	}

	return os;
}

// Paste last of left side with first of right side
static PtokenSequence
glue(PtokenSequence ls, PtokenSequence rs)
{
	if (ls.empty())
		return (rs);
	while (!ls.empty() && ls.back().is_space())
		ls.pop_back();
	while (!rs.empty() && rs.front().is_space())
		rs.pop_front();
	if (ls.empty() && rs.empty())
		return (ls);

	// Glue ls.back() with rs.front()
	Tchar::clear();
	if (!ls.empty()) {
		if (DP()) cout << "glue LS: " << ls.back() << endl;
		Tchar::push_input(ls.back());
		ls.back().set_cpp_str_val();
		ls.pop_back();
	}
	if (!rs.empty()) {
		if (DP()) cout << "glue RS: " << rs.front() << endl;
		Tchar::push_input(rs.front());
		rs.front().set_cpp_str_val();
		rs.pop_front();
	}
	Tchar::rewind_input();
	for (;;) {
		Pltoken t;
		t.getnext<Tchar>();
		if (t.get_code() == EOF)
			break;
		if (DP()) cout << "glue result: " << t << endl;
		ls.push_back(t);
	}
	ls.splice(ls.end(), rs);
	if (DP()) cout << "glue returns: " << ls << endl;
	return (ls);
}

/*
 * Try to ensure that ts has at least one non-space token
 * Return true if this is the case
 * Return any discarded space tokens in removed
 */
static bool
fill_in(PtokenSequence &ts, bool get_more, PtokenSequence &removed)
{
	while (!ts.empty() && ts.front().is_space()) {
		removed.push_back(ts.front());
		ts.pop_front();
	}
	if (!ts.empty())
		return (true);
	if (get_more) {
		Pdtoken t;
		for (;;) {
			t.getnext_noexpand();
			if (t.get_code() == EOF)
				return (false);
			else if (t.is_space())
				removed.push_back(t);
			else
				break;
		}
		ts.push_back(t);
		return (true);
	}
	return (false);
}
