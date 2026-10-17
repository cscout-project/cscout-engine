/*
 * (C) Copyright 2008-2026 Diomidis Spinellis
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
 * CScout invocation options.
 *
 */


#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <vector>

#include <getopt.h>

#include "cpp.h"
#include "pdtoken.h"
#include "fchar.h"
#include "type.h"
#include "ctag.h"
#include "sql.h"
#include "workdb.h"
#include "compiledre.h"
#include "dbtoken.h"
#include "options.h"
#include "util.h"

using namespace std;

// Report usage information and exit
void
usage(char *fname)
{
	cerr << "usage: " << fname <<
		" ["
#ifndef WIN32
		"-b|"	// browse-only
#endif
		"-C|-c|-d D|-d H|-E RE|-o|-M files|"
		"-q|-R URL|-r|-S db|-s db|-v] "
		"[-l file] "

#ifdef PICO_QL
#define PICO_QL_OPTIONS "q"
		"-q|"
#else
#define PICO_QL_OPTIONS ""
#endif

		"[-P RE] [-p port] [-m spec] [-t table ...] file\n"
#ifndef WIN32
		"\t-b\tRun in multiuser browse-only mode\n"
#endif
		"\t-C\tCreate a ctags(1)-compatible tags file\n"
		"\t-c\tProcess the file and exit\n"
		"\t-R URL\tOutput the call graphs specified by the URLs and exit\n"
		"\t-d D\tOutput the #defines being processed\n"
		"\t-d H\tOutput the names of included files being processed\n"
		"\t-E RE\tOutput preprocessed results and exit\n"
		"\t\t(Will process file(s) matched by the regular expression)\n"
		"\t-l file\tSpecify access log file\n"
		"\t-M files\tMerge specified EC files\n"
		"\t-m spec\tSpecify identifiers to monitor (unsound)\n"
		"\t-o\tCreate obfuscated versions of the processed files\n"
		"\t-P RE\tProcess only file(s) matched by the regular expression\n"
		"\t-p port\tSpecify TCP port for serving the CScout web pages\n"
		"\t\t(the port number must be in the range 1024-32767)\n"
#ifdef PICO_QL
		"\t-q\tProvide a PiCO_QL query interface\n"
#else
		"\t-q\tSuppress progress messages on standard error\n"
#endif
		"\t-r\tGenerate an identifier and include file warning report\n"
		"\t-S db\tGenerate the SQL schema for the specified RDBMS\n"
		"\t-s db\tGenerate SQL output for the specified RDBMS\n"
		"\t-t table\tEnable population of the specified RDBMS table\n"
		"\t\t(All enabled by default. Option can be provided multiple times)\n"
		"\t-v\tDisplay version and copyright information and exit\n"
		"\t-3\tEnable the handling of trigraph characters\n"
		;
	exit(1);
}


// Return a compiled RE for the string s, verifying its correctness
static CompiledRE
verified_compiled_re(const char *s)
{
	CompiledRE pre(s, REG_EXTENDED | REG_NOSUB);

	if (!pre.isCorrect()) {
		cerr << "Filename regular expression error:" <<
			pre.getError() << '\n';
		exit(1);
	}
	return pre;
}

void
CscoutOptions::parse_args(int argc, char *argv[])
{
    int c;

	while ((c = getopt(argc, argv, "3bCcd:rvE:P:p:Mm:l:oR:S:s:t:q" PICO_QL_OPTIONS)) != EOF)  //added q for quiet
		switch (c) {
		case '3':
			Fchar::enable_trigraphs();
			break;
		case 'E':
			if (!optarg || process_mode)
				usage(argv[0]);
			// Preprocess the specified file
			Pdtoken::set_preprocessed_output(verified_compiled_re(optarg));
			process_mode = pm_preprocess;
			break;
		case 'C':
			CTag::enable();
			break;
		#ifdef PICO_QL
		case 'q':
			pico_ql = true;
			/* FALLTHROUGH */
		#endif
		case 'c':
			if (process_mode)
				usage(argv[0]);
			process_mode = pm_compile;
			break;
		case 'd':
			if (!optarg)
				usage(argv[0]);
			switch (*optarg) {
			case 'D':	// Similar to gcc -dD
				Pdtoken::set_output_defines();
				break;
			case 'H':	// Similar to gcc -H
				Fchar::set_output_headers();
				break;
			default:
				usage(argv[0]);
			}
			break;
		case 'p':
			if (!optarg)
				usage(argv[0]);
			portno = atoi(optarg);
			if (portno < 1024 || portno > 32767)
				usage(argv[0]);
			break;
		case 'M':
			do_merge = true;
			break;
		case 'm':
			if (!optarg)
				usage(argv[0]);
			monitor = IdQuery(optarg);
			break;
		case 'r':
			if (process_mode)
				usage(argv[0]);
			process_mode = pm_report;
			break;
		case 'v':
			cout << version_info(false);
			exit(0);
		case 'b':
			this->browse_only = true;
			break;
		case 'q':
			quiet = true;
			break;
		case 'l':
			if (!optarg)
				usage(argv[0]);
			log_file = optarg;
			break;
		case 'o':
			if (process_mode)
				usage(argv[0]);
			process_mode = pm_obfuscation;
			break;
		case 'P':
			if (!optarg)
				usage(argv[0]);
			// Process the specified file(s)
			Pdtoken::set_processed_files(verified_compiled_re(optarg));
			break;
		case 'S':
			if (process_mode)
				usage(argv[0]);
			if (!optarg)
				usage(argv[0]);
			db_engine = optarg;
			if (!Sql::setEngine(optarg))
				exit(1);
			workdb_schema(Sql::getInterface(), cout);
			exit(0);
		case 's':
			if (process_mode)
				usage(argv[0]);
			if (!optarg)
				usage(argv[0]);
			process_mode = pm_database;
			db_engine = optarg;
			break;
		case 't':
			if (!optarg)
				usage(argv[0]);
			table_enable(optarg);
			break;
		case 'R':
			if (!optarg)
				usage(argv[0]);
			process_mode = pm_call_graph;
			this->call_graphs.push_back(string(optarg));
			break;
		case '?':
			usage(argv[0]);
		}
}
