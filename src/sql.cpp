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
 * Portable SQL database abstraction
 *
 */

#include <cstring>
#include <iostream>

#include "cpp.h"
#include "sql.h"

// An instance of the database interface
Sql *Sql::instance;

// Return SQL equivalent of character c
const char *
Sql::escape(char c)
{
	static char str[2];

	switch (c) {
	case '\'': return "''";
	default:
		str[0] = c;
		return str;
	}
}

const char *
Hsqldb::escape(char c)
{
	static char str[2];

	switch (c) {
	case '\'': return "''";
	case '\n': return "\\u0000a";
	case '\r': return "\\u0000d";
	default:
		str[0] = c;
		return str;
	}
}

const char *
Sqlite::escape(char c)
{
	static char str[2];

	switch (c) {
	case '\'': return "''";
	case '\n': return "\\u0000a";
	case '\f': return "\\u0000c";
	case '\r': return "\\u0000d";
	default:
		str[0] = c;
		return str;
	}
}

string
Sql::escape(string s)
{
	string r;

	for (string::const_iterator i = s.begin(); i != s.end(); i++)
		r += escape(*i);
	return r;
}

bool
Sql::setEngine(string_view dbengine)
{
	if (dbengine =="mysql")
		instance = new Mysql();
	else if (dbengine =="hsqldb")
		instance = new Hsqldb();
	else if (dbengine =="postgres")
		instance = new Postgres();
	else if (dbengine =="sqlite")
		instance = new Sqlite();
	else {
		cerr << "Unknown database engine " << dbengine << "\n";
		cerr << "Supported database engine types are: hsqldb mysql postgres sqlite\n";
		return false;
	}
	return true;
}

/*
 * Implement very fast database inserts at the risk of
 * possible data corruption in case of a crash.
 * We don't really care, because we assume the database is
 * populated in one go from empty, and if it gets corrupted
 * the process can be repeated.
 * This increases speed in awk.cs from
 * 0m1.59 user 0m13.31 system 1m9.63 elapsed
 * to
 * 0.28 user 0.14 system 0:00.63 elapsed
 * See https://stackoverflow.com/a/58547438/20520 for
 * measurements behind this approach.
 */
const char *
Sqlite::begin_commands()
{
	return "PRAGMA synchronous = OFF;\n"
		"PRAGMA journal_mode = OFF;\n"
		"PRAGMA locking_mode = EXCLUSIVE;\n\n";

};
