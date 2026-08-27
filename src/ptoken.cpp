/*
 * (C) Copyright 2001-2015 Diomidis Spinellis
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

#include <iostream>
#include <map>
#include <string>
#include <deque>
#include <list>
#include <vector>
#include <set>

#include "cpp.h"
#include "error.h"
#include "parse.tab.h"
#include "attr.h"
#include "metrics.h"
#include "fileid.h"
#include "tokid.h"
#include "token.h"
#include "ptoken.h"
#include "ctoken.h"
#include "debug_out.h"

ostream&
operator<<(ostream& o,const Ptoken &t)
{
	o << nest_begin("Ptoken: {")
	    << (Token)t
	    << nest("value") << t.val << '\n';
	if (!t.hideset.empty()) {
		o << nest_begin("hide_set: [");
		for (HideSet::const_iterator i = t.hideset.begin(); i != t.hideset.end(); i++)
			o << *i;
		o << nest_end("]");
	}
	o << nest("producer");
	if (t.get_producer())
		o << *t.get_producer();
	else
		o << "NULL\n";
	o << nest_end("}");
	return (o);
}

ostream& operator<<(ostream& o,const PtokenSequence &ts)
{
	PtokenSequence::const_iterator i;

	o << nest_begin("PtokenSequence: [");
	for (auto t : ts)
		o << t;
	o << nest_end("]");
	return (o);
}

Ptoken::Ptoken(const Ctoken &t) : Token(t)
{
}

/*
 * Set the is_cpp_str_val attribute for the macros that were
 * expanded to yield the stringized or pasted token.
 * Conveniently, these macros are part of the token's hide set.
 */
void
Ptoken::set_cpp_str_val() const
{
	for (auto &tok : hideset)
		tok.set_ec_attribute(is_cpp_str_val);
}


#ifdef UNIT_TEST
// cl -GX -DWIN32 -c eclass.cpp fileid.cpp tokid.cpp tokname.cpp token.cpp
// cl -GX -DWIN32 -DUNIT_TEST ptoken.cpp token.obj tokid.obj eclass.obj tokname.obj fileid.obj kernel32.lib

main()
{
	Ptoken t(IDENTIFIER, "hello");

	cout << t;

	return (0);
}
#endif /* UNIT_TEST */
