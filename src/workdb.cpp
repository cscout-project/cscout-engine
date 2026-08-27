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
 * Export the workspace database as an SQL script
 *
 */

#include <map>
#include <string>
#include <deque>
#include <vector>
#include <stack>
#include <cctype>
#include <iterator>
#include <iostream>
#include <fstream>
#include <list>
#include <set>
#include <sstream>		// ostringstream
#include <cstdio>		// perror

#include "cpp.h"
#include "error.h"
#include "debug.h"
#include "attr.h"
#include "parse.tab.h"
#include "metrics.h"
#include "funmetrics.h"
#include "filemetrics.h"
#include "attr.h"
#include "fileid.h"
#include "filedetails.h"
#include "tokid.h"
#include "token.h"
#include "ctoken.h"
#include "ptoken.h"
#include "fchar.h"
#include "pltoken.h"
#include "macro.h"
#include "pdtoken.h"
#include "eclass.h"
#include "ctoken.h"
#include "type.h"
#include "stab.h"
#include "sql.h"
#include "workdb.h"

// Tables that are disabled (by default none)
static bool disabled_tables[table_max];

// Enable output of the specified table
void
table_enable(const char *name)
{
	static bool initialized;
	static map<string, enum e_table> table_enum;


	if (!initialized) {
		// On first call disable all tables
		for (int i = 0; i < table_max; i++)
			disabled_tables[i] = true;

		table_enum["IDS"] = t_ids;
		table_enum["FILES"] = t_files;
		table_enum["FILEMETRICS"] = t_filemetrics;
		table_enum["TOKENS"] = t_tokens;
		table_enum["COMMENTS"] = t_comments;
		table_enum["STRINGS"] = t_strings;
		table_enum["REST"] = t_rest;
		table_enum["LINEPOS"] = t_linepos;
		table_enum["PROJECTS"] = t_projects;
		table_enum["IDPROJ"] = t_idproj;
		table_enum["FILEPROJ"] = t_fileproj;
		table_enum["LINEPROJ"] = t_lineproj;
		table_enum["DEFINERS"] = t_definers;
		table_enum["INCLUDERS"] = t_includers;
		table_enum["PROVIDERS"] = t_providers;
		table_enum["INCTRIGGERS"] = t_inctriggers;
		table_enum["FUNCTIONS"] = t_functions;
		table_enum["FUNCTIONDEFS"] = t_functiondefs;
		table_enum["FUNCTIONMETRICS"] = t_functionmetrics;
		table_enum["FUNCTIONID"] = t_functionid;
		table_enum["FCALLS"] = t_fcalls;
		table_enum["FILECOPIES"] = t_filecopies;

		initialized = true;
	}

	auto f = table_enum.find(name);
	if (f == table_enum.end()) {
		cerr << "Unkown table name " << name << endl;
		exit(1);
	}
	disabled_tables[f->second] = false;
}

// Return true if the specified table is enabled
bool
table_is_enabled(enum e_table t)
{
	return !disabled_tables[t];
}


// Our identifiers to store as a set
class Identifier {
	Eclass *ec;		// Equivalence class it belongs to
	string id;		// Identifier name
public:
	Identifier() {}
	Identifier(Eclass *e, const string &s) : ec(e), id(s) {}
	string get_id() const { return id; }
	Eclass *get_ec() const { return ec; }
	// To create sets
	inline bool operator ==(const Identifier b) const {
		return (this->ec == b.ec) && (this->id == b.id);
	}
	inline bool operator <(const Identifier b) const {
		int r = this->id.compare(b.id);
		if (r == 0)
			return (this->ec < b.ec);
		else
			return (r < 0);
	}
};

// Insert an equivalence class in the database
static void
insert_eclass(Sql *db, ostream &of, Eclass *e, const string &name)
{
	// Avoid duplicate entries (could also have a dumped Eclass attr)
	static set <Eclass *> dumped;
	if (dumped.find(e) != dumped.end())
		return;
	dumped.insert(e);
	// Update metrics
	id_msum.add_unique_id(e);

	if (table_is_enabled(t_ids))
		of << "INSERT INTO IDS VALUES(" <<
		     ptr_offset(e) << ",'" <<
		     name << "'," <<
		     db->boolval(e->get_attribute(is_readonly)) << ',' <<
		     db->boolval(e->get_attribute(is_undefined_macro)) << ',' <<
		     db->boolval(e->get_attribute(is_undefed_macro)) << ',' <<
		     db->boolval(e->get_attribute(is_redefined_same_macro)) << ',' <<
		     db->boolval(e->get_attribute(is_redefined_diff_macro)) << ',' <<
		     db->boolval(e->get_attribute(is_macro)) << ',' <<
		     db->boolval(e->get_attribute(is_fun_macro)) << ',' <<
		     db->boolval(e->get_attribute(is_macro_arg)) << ',' <<
		     db->boolval(e->get_attribute(is_cpp_const)) << ',' <<
		     db->boolval(e->get_attribute(is_cpp_str_val)) << ',' <<
		     db->boolval(e->get_attribute(is_def_c_const)) << ',' <<
		     db->boolval(e->get_attribute(is_def_not_c_const)) << ',' <<
		     db->boolval(e->get_attribute(is_exp_c_const)) << ',' <<
		     db->boolval(e->get_attribute(is_exp_not_c_const)) << ',' <<
		     db->boolval(e->get_attribute(is_used_in_ice)) << ',' <<

		     db->boolval(e->get_attribute(is_ordinary)) << ',' <<
		     db->boolval(e->get_attribute(is_suetag)) << ',' <<
		     db->boolval(e->get_attribute(is_sumember)) << ',' <<
		     db->boolval(e->get_attribute(is_label)) << ',' <<
		     db->boolval(e->get_attribute(is_typedef)) << ',' <<
		     db->boolval(e->get_attribute(is_enumeration)) << ',' <<
		     db->boolval(e->get_attribute(is_yacc)) << ',' <<
		     db->boolval(e->get_attribute(is_cfunction)) << ',' <<
		     db->boolval(e->get_attribute(is_cscope)) << ',' <<
		     db->boolval(e->get_attribute(is_lscope)) << ',' <<
		     db->boolval(e->is_unused()) <<
		     ");\n";
	// The projects each EC belongs to
	if (table_is_enabled(t_idproj))
		for (unsigned j = attr_end; j < Attributes::get_num_attributes(); j++)
			if (e->get_attribute(j))
				of << "INSERT INTO IDPROJ VALUES("
				    << ptr_offset(e) << ',' << j << ");\n";
}

// Chunk the input into tables
class Chunker {
private:
	fifstream &in;		// Stream we are reading from
	string table;		// Table we are chunking into
	Sql *db;		// Database interface
	ostream &of;		// Stream for writing SQL statements
	Fileid fid;		// File we are chunking
	streampos startpos;	// Starting position of current chunk
	string chunk;		// Characters accumulated in the current chunk
public:
	bool enabled;		// True if output to the table is enabled
	Chunker(fifstream &i, Sql *d, ostream &o, Fileid f) : in(i), table("REST"), db(d), of(o), fid(f), startpos(0), enabled(table_is_enabled(t_rest)) {}

	// Flush the currently collected input into the database
	// Should be called at the point where new input is expected
	void flush() {
		if (chunk.length() > 0) {
			if (enabled)
				of << "INSERT INTO " << table << " VALUES("
				    << fid.get_id()
				    << "," << (unsigned)startpos
				    << ",'" << chunk << "');\n";
			chunk.erase();
		}
		startpos = in.tellg();
	}

	// Start collecting input for a (possibly) new table
	// e specifies whether the table is enabled
	// Should be called at the point where new input is expected
	// s can be input already collected
	void start(const char *t, bool e, const string &s = string("")) {
		flush();
		table = string(t);
		startpos -= s.length();
		chunk = db->escape(s);
		enabled = e;
	}

	void start(const char *t, bool e, char c) {
		start(t, e, string(1, c));
	}

	inline void add(char c) {
		chunk += db->escape(c);
	}
};

// Write out the projects for which the given line is processed.
static void
line_projects_dump(ostream &of, Fileid fid, int line_number)
{
	for (unsigned projid = attr_end; projid < Attributes::get_num_attributes(); projid++)
		if (Filedetails::get_attribute(fid, projid)
		    && Filedetails::is_line_processed(fid, line_number, projid))
			of << "INSERT INTO LINEPROJ VALUES("
			    << fid.get_id()
			    << "," << projid
			    << "," << line_number
			    << ");\n";
}


// Add the contents of a file to the Tokens, Comments, Strings, and Rest tables
// As a side-effect insert corresponding identifiers in the database
// and populate the LineOffset table
static void
file_dump(Sql *db, ostream &of, Fileid fid)
{
	streampos bol(0);			// Beginning of line
	bool at_bol = true;
	int line_number = 1;
	enum e_cfile_state cstate = s_normal;	// C file state machine

	fifstream in;
	in.open(fid.get_path().c_str(), ios::binary);
	if (in.fail()) {
		perror(fid.get_path().c_str());
		exit(1);
	}
	Chunker chunker(in, db, of, fid);
	// Go through the file character by character
	for (;;) {
		Tokid ti;
		int val;

		ti = Tokid(fid, in.tellg());
		if ((val = in.get()) == EOF)
			break;
		char c = (char)val;
		Eclass *ec;

		// Return to normal if slash didn't start a commend
		if (cstate == s_saw_slash && c != '*' && c != '/') {
			chunker.add('/');
			cstate = s_normal;
		}

		if (cstate != s_block_comment &&
		    cstate != s_string &&
		    cstate != s_cpp_comment &&
		    (isalnum(c) || c == '_') &&
		    (ec = ti.check_ec()) &&
		    ec->is_identifier()) {
			// Process identifiers
			id_msum.add_pre_cpp_id(ec);
			string s;
			s = (char)val;
			int len = ec->get_len();
			for (int j = 1; j < len; j++)
				s += (char)in.get();
			insert_eclass(db, of, ec, s);
			Filedetails::get_pre_cpp_metrics(fid).process_identifier(s, ec);
			chunker.flush();
			if (table_is_enabled(t_tokens))
				of << "INSERT INTO TOKENS VALUES("
				    << fid.get_id() << ","
				    << (unsigned)ti.get_streampos() << ","
				    << ptr_offset(ec) << ");\n";
		} else {
			Filedetails::get_pre_cpp_metrics(fid).process_char(c);
			if (at_bol) {
				if (table_is_enabled(t_linepos))
					of << "INSERT INTO LINEPOS VALUES("
					    << fid.get_id()
					    << "," << (unsigned)bol
					    << "," << line_number
					    << ");\n";
				if (table_is_enabled(t_linepos))
					line_projects_dump(of, fid, line_number);
				at_bol = false;
			}
			if (c == '\n') {
				at_bol = true;
				bol = in.tellg();
				line_number++;
			}
			switch (cstate) {
			case s_normal:
				if (c == '/')
					cstate = s_saw_slash;
				else if (c == '"') {
					cstate = s_string;
					chunker.start("STRINGS", table_is_enabled(t_strings), c);
				} else if (c == '\'') {
					cstate = s_char;
					chunker.add(c);
				} else
					chunker.add(c);
				break;
			case s_char:
				chunker.add(c);
				if (c == '\'')
					cstate = s_normal;
				else if (c == '\\')
					cstate = s_saw_chr_backslash;
				break;
			case s_string:
				chunker.add(c);
				if (c == '"') {
					cstate = s_normal;
					chunker.start("REST", table_is_enabled(t_rest));
				} else if (c == '\\')
					cstate = s_saw_str_backslash;
				break;
			case s_saw_chr_backslash:
				chunker.add(c);
				cstate = s_char;
				break;
			case s_saw_str_backslash:
				chunker.add(c);
				cstate = s_string;
				break;
			case s_saw_slash:		// After a / character
				if (c == '/') {
					cstate = s_cpp_comment;
					chunker.start("COMMENTS", table_is_enabled(t_comments), "//");
				} else if (c == '*') {
					cstate = s_block_comment;
					chunker.start("COMMENTS", table_is_enabled(t_comments), "/*");
				} else {
					// Should have set s_normal at the top
					csassert(0);
				}
				break;
			case s_cpp_comment:		// Inside C++ comment
				chunker.add(c);
				if (c == '\n') {
					cstate = s_normal;
					chunker.start("REST", table_is_enabled(t_rest));
				}
				break;
			case s_block_comment:		// Inside C block comment
				chunker.add(c);
				if (c == '*')
					cstate = s_block_star;
				break;
			case s_block_star:		// Found a * in a block comment
				chunker.add(c);
				if (c == '/') {
					cstate = s_normal;
					chunker.start("REST", table_is_enabled(t_rest));
				} else if (c != '*')
					cstate = s_block_comment;
				break;
			default:
				csassert(0);
			}
		}
	}
	chunker.flush();
}


void
workdb_schema(Sql *db, ostream &of)
{
	if (table_is_enabled(t_ids)) of <<
		"-- Details of interdependant identifiers appearing in the workspace\n"
		"CREATE TABLE IDS(\n"
		"  EID " << db->ptrtype() << " PRIMARY KEY, -- Unique identifier key\n"
		"  NAME " << db->varchar() << ", -- Identifier name\n"
		"  READONLY " << db->booltype() << ", -- True if it appears in at least one read-only file\n"
		"  UNDEFMACRO " << db->booltype() << ", -- True if it is apparantly an undefined macro\n"
		"  UNDEFEDMACRO " << db->booltype() << ", -- True if the macro has been #undefed\n"
		"  REDEFEDSAMEMACRO " << db->booltype() << ", -- True if the macro has been #defined again with same value\n"
		"  REDEFEDDIFFMACRO " << db->booltype() << ", -- True if the macro has been #defined again with different value\n"
		"  MACRO " << db->booltype() << ", -- True if it a preprocessor macro\n"
		"  FUNMACRO " << db->booltype() << ", -- True if it a preprocessor function-like macro\n"
		"  MACROARG " << db->booltype() << ", -- True if it a preprocessor macro argument\n"
		"  CPPCONST " << db->booltype() << ", -- True if used in a preprocessor constant\n"
		"  CPPSTRVAL " << db->booltype() << ", -- True if macro value is used as a preprocessor string operand\n"
		"  DEFCCONSTVAL " << db->booltype() << ", -- True if macro value defined as a C compile time constant\n"
		"  NOTDEFCCONSTVAL " << db->booltype() << ", -- True if macro value defined as not a C compile time constant\n"
		"  EXPCCONSTVAL " << db->booltype() << ", -- True if macro value expanded as a C compile time constant\n"
		"  NOTEXPCCONSTVAL " << db->booltype() << ", -- True if macro value expanded as not a C compile time constant\n"
		"  ISUSEDINICE " << db->booltype() << ", -- True if macro value is used in a C integer constant expression context\n"

		"  ORDINARY " << db->booltype() << ", -- True if it is an ordinary identifier (variable or function)\n"
		"  SUETAG " << db->booltype() << ", -- True if it is a structure, union, or enumeration tag\n"
		"  SUMEMBER " << db->booltype() << ", -- True if it is a structure or union member\n"
		"  LABEL " << db->booltype() << ", -- True if it is a label\n"
		"  TYPEDEF " << db->booltype() << ", -- True if it is a typedef\n"
		"  ENUM " << db->booltype() << ", -- True if it is an enumeration member\n"
		"  YACC " << db->booltype() << ", -- True if it is a yacc identifier\n"
		"  FUN " << db->booltype() << ", -- True if it is a function name\n"
		"  CSCOPE " << db->booltype() << ", -- True if its scope is a compilation unit\n"
		"  LSCOPE " << db->booltype() << ", -- True if it has linkage scope\n"
		"  UNUSED " << db->booltype() << " -- True if it is not used\n"
		");\n";

	if (table_is_enabled(t_files)) of <<
		"\n\n-- File details\n"
		"CREATE TABLE FILES(\n"
		"  FID INTEGER PRIMARY KEY, -- Unique file key\n"
		"  NAME " << db->varchar() << ", -- File name\n"
		"  RO " << db->booltype() << " -- True if the file is read-only\n"
		");\n";

	if (table_is_enabled(t_filemetrics)) {
		of
		    << "\n\n-- File metrics\n"
		    << "CREATE TABLE FILEMETRICS(\n"
		    "  FID INTEGER, -- File key\n"
		    "  PRECPP "
		    << db->booltype()
		    << ", -- True for values before the cpp false for values after it\n";

		for (int i = 0; i < FileMetrics::metric_max; i++)
			if (!Metrics::is_internal<FileMetrics>(i))
				of
				    << "  "
				    << Metrics::get_dbfield<FileMetrics>(i)
				    << " INTEGER, -- "
				    << Metrics::get_name<FileMetrics>(i)
				    << "\n";
		of <<
		    "  PRIMARY KEY(FID, PRECPP),\n"
		    "  FOREIGN KEY(FID) REFERENCES FILES(FID)\n"
		    ");\n";
}

	if (table_is_enabled(t_tokens)) of <<
		"\n\n-- Instances of identifier tokens within the source code\n"
		"CREATE TABLE TOKENS(\n"
		"  FID INTEGER, -- File key\n"
		"  FOFFSET INTEGER, -- Offset within the file\n"
		"  EID " << db->ptrtype() << ", -- Identifier key\n"
		"  PRIMARY KEY(FID, FOFFSET),\n"
		"  FOREIGN KEY(FID) REFERENCES FILES(FID),\n"
		"  FOREIGN KEY(EID) REFERENCES IDS(EID)\n"
		");\n";

	if (table_is_enabled(t_comments)) of <<
		"\n\n-- Comments in the code\n"
		"CREATE TABLE COMMENTS(\n"
		"  FID INTEGER, -- File key\n"
		"  FOFFSET INTEGER, -- Offset within the file\n"
		"  COMMENT " << db->varchar() << ", -- The comment, including its delimiters\n"
		"  PRIMARY KEY(FID, FOFFSET),\n"
		"  FOREIGN KEY(FID) REFERENCES FILES(FID)\n"
		");\n";

	if (table_is_enabled(t_strings)) of <<
		"\n\n-- Strings in the code\n"
		"CREATE TABLE STRINGS(\n"
		"  FID INTEGER, -- File key\n"
		"  FOFFSET INTEGER, -- Offset within the file\n"
		"  STRING " << db->varchar() << ", -- The string, including its delimiters\n"
		"  PRIMARY KEY(FID, FOFFSET),\n"
		"  FOREIGN KEY(FID) REFERENCES FILES(FID)\n"
		");\n";

	if (table_is_enabled(t_rest)) of <<
		"\n\n-- Remaining, non-identifier source code\n"
		"CREATE TABLE REST(\n"
		"  FID INTEGER, -- File key\n"
		"  FOFFSET INTEGER, -- Offset within the file\n"
		"  CODE " << db->varchar() << ", -- The actual code\n"
		"  PRIMARY KEY(FID, FOFFSET),\n"
		"  FOREIGN KEY(FID) REFERENCES FILES(FID)\n"
		");\n";

	if (table_is_enabled(t_linepos)) of <<
		"\n\n-- Line number offsets within each file\n"
		"CREATE TABLE LINEPOS(\n"
		"  FID INTEGER, -- File key\n"
		"  FOFFSET INTEGER, -- Offset within the file\n"
		"  LNUM INTEGER, -- Line number (starts at 1)\n"
		"  PRIMARY KEY(FID, FOFFSET),\n"
		"  FOREIGN KEY(FID) REFERENCES FILES(FID)\n"
		");\n";


	if (table_is_enabled(t_projects)) of <<
		"\n\n-- Project details\n"
		"CREATE TABLE PROJECTS(\n"
		"  PID INTEGER PRIMARY KEY, -- Unique project key\n"
		"  NAME " << db->varchar() << " -- Project name\n"
		");\n";

	if (table_is_enabled(t_idproj)) of <<
		"\n\n-- Identifiers appearing in projects\n"
		"CREATE TABLE IDPROJ(\n"
		"  EID " << db->ptrtype() << ", -- Identifier key\n"
		"  PID INTEGER, -- Project key\n"
		"  PRIMARY KEY(EID, PID),\n"
		"  FOREIGN KEY(EID) REFERENCES IDS(EID),\n"
		"  FOREIGN KEY(PID) REFERENCES PROJECTS(PID)\n"
		");\n";

	if (table_is_enabled(t_fileproj)) of <<
		"\n\n-- Files used in projects\n"
		"CREATE TABLE FILEPROJ(\n"
		"  FID INTEGER, -- File key\n"
		"  PID INTEGER, -- Project key\n"
		"  PRIMARY KEY(PID, FID),\n"
		"  FOREIGN KEY(FID) REFERENCES FILES(FID),\n"
		"  FOREIGN KEY(PID) REFERENCES PROJECTS(PID)\n"
		");\n";

	if (table_is_enabled(t_lineproj)) of <<
		"\n\n-- Lines processed in projects\n"
		"CREATE TABLE LINEPROJ(\n"
		"  FID INTEGER, -- File key\n"
		"  PID INTEGER, -- Project key\n"
		"  LNUM INTEGER, -- Line number (starts at 1)\n"
		"  PRIMARY KEY(PID, FID, LNUM),\n"
		"  FOREIGN KEY(FID) REFERENCES FILES(FID),\n"
		"  FOREIGN KEY(PID) REFERENCES PROJECTS(PID)\n"
		");\n";

	of <<
	    "\n\n"
	    "-- Foreign keys for the following four tables are not specified, because it is\n" 
	    "-- difficult to satisfy integrity constraints: files (esp. their metrics,\n" 
	    "-- esp. ncopies) can't be written until the end of processing, while\n" 
	    "-- to conserve space, these tables are written after each file is processed.\n" 
	    "-- Alternatively, inserts to these tables could be wrapped into\n" 
	    "-- SET REFERENTIAL_INTEGRITY { TRUE | FALSE } calls.\n";

	if (table_is_enabled(t_definers)) of <<
		"\n\n-- Included files defining required elements for a given compilation unit and project\n"
		"CREATE TABLE DEFINERS(\n"
		"  PID INTEGER, -- Project key\n"
		"  CUID INTEGER, -- Compilation unit key\n"
		"  BASEFILEID INTEGER, -- File (often .c) requiring (using) a definition\n"
		"  DEFINERID INTEGER -- File (often .h) providing a definition\n"
		"  -- FOREIGN KEY(PID) REFERENCES PROJECTS(PID),\n"
		"  -- FOREIGN KEY(CUID) REFERENCES FILES(FID),\n"
		"  -- FOREIGN KEY(BASEFILEID) REFERENCES FILES(FID),\n"
		"  -- FOREIGN KEY(DEFINERID) REFERENCES FILES(FID)\n"
		");\n";

	if (table_is_enabled(t_includers)) of <<
		"\n\n-- Included files including files for a given compilation unit and project\n"
		"CREATE TABLE INCLUDERS(\n"
		"  PID INTEGER, -- Project key\n"
		"  CUID INTEGER, -- Compilation unit key\n"
		"  BASEFILEID INTEGER, -- File included in the compilation\n"
		"  INCLUDERID INTEGER -- Files that include it\n"
		"  -- FOREIGN KEY(PID) REFERENCES PROJECTS(PID),\n"
		"  -- FOREIGN KEY(CUID) REFERENCES FILES(FID),\n"
		"  -- FOREIGN KEY(BASEFILEID) REFERENCES FILES(FID),\n"
		"  -- FOREIGN KEY(INCLUDERID) REFERENCES FILES(FID)\n"
		");\n";

	if (table_is_enabled(t_providers)) of <<
		"\n\n-- Included files providing code or data for a given compilation unit and project\n"
		"CREATE TABLE PROVIDERS(\n"
		"  PID INTEGER, -- Project key\n"
		"  CUID INTEGER, -- Compilation unit key\n"
		"  PROVIDERID INTEGER -- Included file\n"
		"  -- FOREIGN KEY(PID) REFERENCES PROJECTS(PID),\n"
		"  -- FOREIGN KEY(CUID) REFERENCES FILES(FID),\n"
		"  -- FOREIGN KEY(PROVIDERID) REFERENCES FILES(FID)\n"
		");\n";

	if (table_is_enabled(t_inctriggers)) of <<
		"\n\n-- Tokens requiring file inclusion for a given compilation unit and project\n"
		"CREATE TABLE INCTRIGGERS(\n"
		"  PID INTEGER, -- Project key\n"
		"  CUID INTEGER, -- Compilation unit key\n"
		"  BASEFILEID INTEGER, -- File requiring a definition\n"
		"  DEFINERID INTEGER, -- File providing a definition\n"
		"  FOFFSET INTEGER, -- Definition's offset within the providing file\n"
		"  LEN INTEGER -- Token's length\n"
		"  -- FOREIGN KEY(PID) REFERENCES PROJECTS(PID),\n"
		"  -- FOREIGN KEY(CUID) REFERENCES FILES(FID),\n"
		"  -- FOREIGN KEY(BASEFILEID) REFERENCES FILES(FID),\n"
		"  -- FOREIGN KEY(DEFINERID) REFERENCES FILES(FID)\n"
		");\n";

	if (table_is_enabled(t_functions)) of <<
		"\n\n-- C functions and function-like macros\n"
		"CREATE TABLE FUNCTIONS(\n"
		"  ID " << db->ptrtype() << " PRIMARY KEY, -- Unique function identifier\n"
		"  NAME " << db->varchar() << ", -- Function name (denormalized; see the FUNCTIONID table)\n"
		"  ISMACRO " << db->booltype() << ", -- True if a function-like macro (otherwise a C function)\n"
		"  DEFINED " << db->booltype() << ", -- True if the function is defined within the workspace\n"
		"  DECLARED " << db->booltype() << ", -- True if the function is declared within the workspace\n"
		"  FILESCOPED " << db->booltype() << ", -- True if the function's scope is a single compilation unit (static or macro)\n"
		"  FID INTEGER, -- File key of the function's definition, declaration, or use\n"
		"  FOFFSET INTEGER, -- Offset of definition, declaration, or use within the file\n"
		"  FANIN INTEGER, -- Fan-in (number of callers)\n"
		"  FOREIGN KEY(FID) REFERENCES FILES(FID)\n"
		");\n";

	if (table_is_enabled(t_functiondefs)) of <<
		"\n\n-- Details of defined functions and macros\n"
		"CREATE TABLE FUNCTIONDEFS(\n"
		"  FUNCTIONID " << db->ptrtype() << " PRIMARY KEY, -- Function identifier key\n"
		"  FIDBEGIN INTEGER, -- File key of the function's definition begin\n"
		"  FOFFSETBEGIN INTEGER, -- Offset of definition begin within the file\n"
		"  FIDEND INTEGER, -- File key of the function's definition end\n"
		"  FOFFSETEND INTEGER, -- Offset of definition end within the file\n"
		"  FOREIGN KEY(FUNCTIONID) REFERENCES FUNCTIONS(ID)\n"
		");\n";

	if (table_is_enabled(t_functionmetrics)) {
		of
		    << "\n\n-- Metrics of defined functions and macros\n"
		    << "CREATE TABLE FUNCTIONMETRICS(\n"
		    "  FUNCTIONID " << db->ptrtype() << ", -- Function identifier key\n"
		    "  PRECPP " << db->booltype() << ", -- True for values before the cpp false for values after it\n";

		for (int i = 0; i < FunMetrics::metric_max; i++)
			if (!Metrics::is_internal<FunMetrics>(i))
				of
				    << "  "
				    << Metrics::get_dbfield<FunMetrics>(i)
				    << (i >= FunMetrics::em_real_start
					? " REAL" : " INTEGER")
				    << ", -- "
				    << Metrics::get_name<FunMetrics>(i)
				    << "\n";
		of <<
		    "  PRIMARY KEY(FUNCTIONID, PRECPP),\n"
		    "  FOREIGN KEY(FUNCTIONID) REFERENCES FUNCTIONS(ID)\n"
		    ");\n";
	}

	if (table_is_enabled(t_functionid)) of <<
		"\n\n-- Identifiers comprising a function's name\n"
		"CREATE TABLE FUNCTIONID(\n"
		"  FUNCTIONID " << db->ptrtype() << ", -- Function identifier key\n"
		"  ORDINAL INTEGER, -- Position of the identifier within the function name (0-based)\n"
		"  FID INTEGER, -- File key of the identifier\n"
		"  FOFFSET INTEGER, -- Offset of the identifier within the file\n"
		"  LEN INTEGER, -- Length of the identifier part\n"
		"  PRIMARY KEY(FUNCTIONID, ORDINAL),\n"
		"  FOREIGN KEY(FUNCTIONID) REFERENCES FUNCTIONS(ID),\n"
		"  FOREIGN KEY(FID) REFERENCES FILES(FID)\n"
		");\n";

	if (table_is_enabled(t_fcalls)) of <<
		"\n\n-- Function calls\n"
		"CREATE TABLE FCALLS(\n"
		"  SOURCEID " << db->ptrtype() << ", -- Calling function identifier key\n"
		"  DESTID " << db->ptrtype() << ", -- Called function identifier key\n"
		"  FOREIGN KEY(SOURCEID) REFERENCES FUNCTIONS(ID),\n"
		"  FOREIGN KEY(DESTID) REFERENCES FUNCTIONS(ID)\n"
		");\n";

	if (table_is_enabled(t_filecopies)) of <<
		"\n\n-- Files occuring in more than one copy\n"
		"CREATE TABLE FILECOPIES(\n"
		"  GROUPID INTEGER, -- File group identifier\n"
		"  FID INTEGER, -- Key of file belonging to a group of identical files\n"
		"  PRIMARY KEY(GROUPID, FID),\n"
		"  FOREIGN KEY(FID) REFERENCES FILES(FID)\n"
		");\n";
}

void
workdb_rest(Sql *db, ostream &of)
{
	// Project names
	const Project::proj_map_type &m = Project::get_project_map();
	Project::proj_map_type::const_iterator pm;
	if (table_is_enabled(t_projects))
		for (pm = m.begin(); pm != m.end(); pm++)
			of << "INSERT INTO PROJECTS VALUES("
				<< pm->second << ",'" << pm->first << "');\n";

	vector <Fileid> files = Fileid::files(true);

	int groupnum = 0;

	// Details and contents of each file
	// As a side effect populate the EC identifier member
	for (vector <Fileid>::iterator i = files.begin(); i != files.end(); i++) {
		if (table_is_enabled(t_files)) {
			of << "INSERT INTO FILES VALUES("
			    << i->get_id() << ",'"
			    << i->get_path() << "',"
			    << db->boolval(i->get_readonly());
			of << ");\n";
			// Pre-cpp
			of << "INSERT INTO FILEMETRICS VALUES("
			    << i->get_id() << ","
			    << db->boolval(true);
			for (int j = 0; j < FileMetrics::metric_max; j++) {
				if (Metrics::is_internal<FileMetrics>(j))
					continue;
				if (Metrics::is_pre_cpp<FileMetrics>(j))
					of << ',' << Filedetails::get_pre_cpp_metrics(*i).get_metric(j);
				else
					of << ",NULL";
			}
			of << ");\n";
			// Post-cpp
			of << "INSERT INTO FILEMETRICS VALUES("
			    << i->get_id() << ","
			    << db->boolval(false);
			for (int j = 0; j < FileMetrics::metric_max; j++) {
				if (Metrics::is_internal<FileMetrics>(j))
					continue;
				if (Metrics::is_post_cpp<FileMetrics>(j))
					of << ',' << Filedetails::get_post_cpp_metrics(*i).get_metric(j);
				else
					of << ",NULL";
			}
			of << ");\n";
		}
		// This invalidates the file's metrics
		file_dump(db, of, *i);
		// The projects this file belongs to
		for (unsigned j = attr_end; j < Attributes::get_num_attributes(); j++)
			if (Filedetails::get_attribute(*i, j) && table_is_enabled(t_fileproj))
				of << "INSERT INTO FILEPROJ VALUES("
				     << i->get_id() << ',' << j << ");\n";

		// Copies of the file
		const set <Fileid> &copies(Filedetails::get_identical_files(*i));
		if (copies.size() > 1
		    && table_is_enabled(t_filecopies)
		    && copies.begin()->get_id() == i->get_id()) {
			for (set <Fileid>::const_iterator j = copies.begin(); j != copies.end(); j++)
				of << "INSERT INTO FILECOPIES VALUES("
				     << groupnum << ',' << j->get_id()
				     << ");\n";
			groupnum++;
		}
	}
}
