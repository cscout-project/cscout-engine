/*
 * (C) Copyright 2008-2024 Diomidis Spinellis
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
#include "fdep.h"
#include "call.h"
#include "fcall.h"
#include "mcall.h"
#include "eclass.h"
#include "sql.h"
#include "workdb.h"
#include "globobj.h"

// All global objects
GlobObj::glob_map GlobObj::all;

// Mark that file ref uses in its runtime image a global object defined in file def
static void
mark_runtime_use(Fileid def, Fileid ref)
{
	Filedetails::set_glob_uses(ref, def);
	Filedetails::set_glob_used_by(def, ref);
}

void
GlobObj::set_file_dependencies()
{
	for (glob_map::iterator i = all.begin(); i != all.end(); i++)
		for (set <Fileid>::iterator def = i->second->defined.begin(); def != i->second->defined.end(); def++)
			for (set <Fileid>::iterator ref = i->second->used.begin(); ref != i->second->used.end(); ref++)
				if (*ref != *def) {
					if (DP())
						cout << i->second->get_name() << ": " << ref->get_fname() << " uses " << def->get_fname() << endl;
					mark_runtime_use(*def, *ref);
				}
}

GlobObj *
GlobObj::get_glob(const Token &t)
{
	pair <glob_map::const_iterator, glob_map::const_iterator> maybe(all.equal_range(t.get_parts_begin()->get_tokid()));
	glob_map::const_iterator i;

	for (i = maybe.first; i != maybe.second; i++) {
		if (DP())
			cout << "Compare " << t << " with " << i->second->get_token() << "\n";
		if (t.equals(i->second->get_token())) {
			if (DP())
				cout << "Get glob for " << t << " returns " << &(i->second) << "\n";
			return i->second;
		}
	}
	return NULL;
}

// ctor; never call it if the call for t already exists
GlobObj::GlobObj(const Token &t, Type typ, const string &s) :
		name(s),
		token(t),
		type(typ)
{
	if (DP())
		cout << "Construct new globobj for " << s << endl;
	all.insert(glob_map::value_type(t.get_parts_begin()->get_tokid(), this));
}

// Dump, for each file, the files it depends on for global objects
void
GlobObj::dumpSql(Sql *, ostream &of)
{
	if (!table_is_enabled(t_globuses))
		return;

	vector <Fileid> files(Fileid::files(true));
	for (vector <Fileid>::const_iterator file = files.begin(); file != files.end(); file++)
		for (Fileidset::const_iterator def = Filedetails::get_glob_uses(*file).begin(); def != Filedetails::get_glob_uses(*file).end(); def++)
			of << "INSERT INTO GLOBUSES VALUES(" <<
			    def->get_id() << ',' <<
			    file->get_id() << ");\n";
}
