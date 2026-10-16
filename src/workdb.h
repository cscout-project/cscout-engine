/*
 * (C) Copyright 1995-2025 Diomidis Spinellis
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
 * SQL back-end
 *
 */

#ifndef WORKDB_
#define WORKDB_

#include <iostream>

using namespace std;

#include "sql.h"

// Keep this in sync with table_enable in workdb.cpp
enum e_table {
	t_ids,
	t_files,
	t_filemetrics,
	t_tokens,
	t_comments,
	t_strings,
	t_rest,
	t_linepos,
	t_projects,
	t_idproj,
	t_fileproj,
	t_lineproj,
	t_definers,
	t_includers,
	t_providers,
	t_inctriggers,
	t_functions,
	t_functiondefs,
	t_functionmetrics,
	t_functionid,
	t_fcalls,
	t_filecopies,
	t_metadata,
	t_globuses,
	table_max
};

// Enable output of the specified table
void table_enable(const char *name);

// Return true if the specified table is enabled
bool table_is_enabled(enum e_table t);

// Output the database schema
void workdb_schema(Sql *db, ostream &of);

// Output database data
void workdb_rest(Sql *db, ostream &of);
// Output database metadata
void workdb_metadata(Sql *db, ostream &of, const string &input_file_path);

#endif // WORKDB_
