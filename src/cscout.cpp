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
 * CLI and web-based interface for viewing and processing C code
 * Important functions: main(), file_analyze(), file_refactor()
 *
 */

#include <map>
#include <string>
#include <deque>
#include <vector>
#include <stack>
#include <iterator>
#include <iostream>
#include <fstream>
#include <list>
#include <set>
#include <utility>
#include <functional>
#include <algorithm>		// set_difference
#include <cctype>
#include <sstream>		// ostringstream
#include <cstdio>		// perror, rename
#include <cstdlib>		// atoi
#include <cerrno>		// errno
#include <regex.h> // regex

#include <getopt.h>

#include "swill.h"

#include "cpp.h"
#include "debug.h"
#include "error.h"
#include "parse.tab.h"
#include "attr.h"
#include "metrics.h"
#include "fileid.h"
#include "tokid.h"
#include "token.h"
#include "ptoken.h"
#include "fchar.h"
#include "pltoken.h"
#include "macro.h"
#include "pdtoken.h"
#include "eclass.h"
#include "type.h"
#include "stab.h"
#include "fdep.h"
#include "version.h"
#include "call.h"
#include "fcall.h"
#include "mcall.h"
#include "option.h"
#include "query.h"
#include "mquery.h"
#include "idquery.h"
#include "funquery.h"
#include "filequery.h"
#include "logo.h"
#include "pager.h"
#include "html.h"
#include "dirbrowse.h"
#include "fileutils.h"
#include "globobj.h"
#include "fifstream.h"
#include "ctag.h"
#include "timer.h"
#include "dbtoken.h"
#include "macro_arg_processor.h"

#ifdef PICO_QL
#include "pico_ql_search.h"
using namespace picoQL;
#endif

#include "sql.h"
#include "workdb.h"
#include "obfuscate.h"
#include "progress.h"
#include "util.h"
#include "progress.h"
#include "options.h"
#include "engine.h"
#include "progress.h"

CscoutOptions opts;
CscoutEngine engine(opts);

#define ids Identifier::ids

#define prohibit_remote_access(file)
#define prohibit_browsers(file) \
	do { \
		if (browse_only) { \
			nonbrowse_operation_prohibited(file); \
			return 0; \
		} \
	} while (0)



// Workspace modification state
static enum e_modification_state {
	ms_unmodified,			// Unmodified; can be modified
	ms_subst,			// An identifier has been substituted; only further substitutions are allowed
	ms_hand_edit			// A file has been hand-edited; only further hand-edits are allowed
} modification_state;


// This uses many of the above, and is therefore included here
#include "gdisplay.h"

// Set to true when the user has specified the application to exit
static bool must_exit = false;


// Set to true if we operate in browsing mode
static bool browse_only = false;
// Maximum number of nodes and edges allowed to browsing-only clients
#define MAX_BROWSING_GRAPH_ELEMENTS 1000




Attributes::size_type current_project;

static int index_page(FILE *of, void *data);

// Return the page suffix for the select call graph type
static const char *
graph_suffix()
{
	switch (Option::cgraph_type->get()) {
	case 't': return ".txt";
	case 'h': return ".html";
	case 'd': return "_dot.txt";
	case 's': return ".svg";
	case 'g': return ".gif";
	case 'p': return ".png";
	case 'f': return ".pdf";
	}
	return "";
}

// Display an identifier hyperlink
static void
html(FILE *of, const IdPropElem &i)
{
	fprintf(of, "<a href=\"id.html?id=%p\">", (void *)i.first);
	html_string(of, (i.second).get_id());
	fputs("</a>", of);
}

static void
html(FILE *of, const Call &c)
{
	fprintf(of, "<a href=\"fun.html?f=%p\">", (void *)&c);
	html_string(of, c.get_name());
	fputs("</a>", of);
}

// Display a hyperlink based on a string and its starting tokid
static void
html_string(FILE *of, const string &s, Tokid t)
{
	int len = s.length();
	for (int pos = 0; pos < len;) {
		Eclass *ec = t.get_ec();
		Identifier id(ec, s.substr(pos, ec->get_len()));
		const IdPropElem ip(ec, id);
		html(of, ip);
		pos += ec->get_len();
		t += ec->get_len();
		if (pos < len)
			fprintf(of, "][");
	}
}

// Display hyperlinks to a function's identifiers
static void
html_string(FILE *of, const Call *f)
{
	int start = 0;
	for (dequeTpart::const_iterator i = f->get_token().get_parts_begin(); i != f->get_token().get_parts_end(); i++) {
		Tokid t = i->get_tokid();
		putc('[', of);
		html_string(of, f->get_name().substr(start, i->get_len()), t);
		putc(']', of);
		start += i->get_len();
	}
}

// Display the contents of a file in hypertext form
static void
file_hypertext(FILE *of, Fileid fi, bool eval_query)
{
	istream *in;
	const string &fname = fi.get_path();
	bool at_bol = true;
	int line_number = 1;
	bool mark_unprocessed = !!swill_getvar("marku");

	/*
	 * In theory this could be handled by adding a class
	 * factory method to Query, and making eval virtual.
	 * In practice the IdQuery and FunQuery eval methods
	 * take incompatible arguments, and are difficult to
	 * reconcile.
	 */
	IdQuery idq;
	FunQuery funq;
	bool have_funq, have_idq;
	char *qtype = swill_getvar("qt");
	have_funq = have_idq = false;
	if (!qtype || strcmp(qtype, "id") == 0) {
		idq = IdQuery(of, Option::file_icase->get(), current_project, eval_query);
		have_idq = true;
	} else if (strcmp(qtype, "fun") == 0) {
		funq = FunQuery(of, Option::file_icase->get(), current_project, eval_query);
		have_funq = true;
	} else {
		fprintf(stderr, "Unknown query type (try adding &qt=id to the URL).\n");
		return;
	}

	if (DP())
		cout << "Write to " << fname << endl;
	if (Filedetails::is_hand_edited(fi)) {
		in = new istringstream(Filedetails::get_original_contents(fi));
		fputs("<p>This file has been edited by hand. The following code reflects the contents before the first CScout-invoked hand edit.</p>", of);
	} else {
		in = new ifstream(fname.c_str(), ios::binary);
		if (in->fail()) {
			html_perror(of, "Unable to open " + fname + " for reading");
			return;
		}
	}
	fputs("<hr><code>", of);
	(void)html('\n');	// Reset HTML tab handling
	// Go through the file character by character
	for (;;) {
		Tokid ti;
		int val;

		ti = Tokid(fi, in->tellg());
		if ((val = in->get()) == EOF)
			break;
		if (at_bol) {
			fprintf(of,"<a name=\"%d\"></a>", line_number);
			if (mark_unprocessed && !Filedetails::is_line_processed(fi, line_number))
				fprintf(of, "<span class=\"unused\">");
			if (Option::show_line_number->get()) {
				char buff[50];
				snprintf(buff, sizeof(buff), "%5d ", line_number);
				// Do not go via HTML string to keep tabs ok
				for (char *s = buff; *s; s++)
					if (*s == ' ')
						fputs("&nbsp;", of);
					else
						fputc(*s, of);
			}
			at_bol = false;
		}
		// Identifier we can mark
		Eclass *ec;
		if (have_idq && (ec = ti.check_ec()) && ec->is_identifier() && idq.need_eval()) {
			string s;
			s = (char)val;
			int len = ec->get_len();
			for (int j = 1; j < len; j++)
				s += (char)in->get();
			Identifier i(ec, s);
			const IdPropElem ip(ec, i);
			if (idq.eval(ip))
				html(of, ip);
			else
				html_string(of, s);
			continue;
		}
		// Function we can mark
		if (have_funq && funq.need_eval()) {
			pair <Call::const_fmap_iterator_type, Call::const_fmap_iterator_type> be(Call::get_calls(ti));
			Call::const_fmap_iterator_type ci;
			for (ci = be.first; ci != be.second; ci++)
				if (funq.eval(ci->second)) {
					string s;
					s = (char)val;
					int len = ci->second->get_name().length();
					for (int j = 1; j < len; j++)
						s += (char)in->get();
					html(of, *(ci->second));
					break;
				}
			if (ci != be.second)
				continue;
		}
		fprintf(of, "%s", html((char)val));
		if ((char)val == '\n') {
			at_bol = true;
			if (mark_unprocessed && !Filedetails::is_line_processed(fi, line_number))
				fprintf(of, "</span>");
			line_number++;
		}
	}
	delete in;
	fputs("<hr></code>", of);
}


static void
change_prohibited(FILE *fo)
{
	html_head(fo, "nochange", "Change Prohibited");
	fputs("Identifier substitutions or function argument refactoring are not allowed "
		"to be performed together with and the hand-editing of files"
		"within the same CScout session.", fo);
	html_tail(fo);
}

static void
nonbrowse_operation_prohibited(FILE *fo)
{
	html_head(fo, "nochange", "Non-browsing Operations Disabled");
	fputs("This is a multiuser browse-only CScout session."
		"Non-browsing operations are disabled.", fo);
	html_tail(fo);
}

// Call before the start of a file list
static void
html_file_begin(FILE *of)
{
	if (Option::fname_in_context->get())
		fprintf(of, "<table class='dirlist'><tr><th>Directory</th><th>File</th>");
	else
		fprintf(of, "<table><tr><th></th><th></th>");
}

// Call before actually listing files (after printing additional headers)
static void
html_file_set_begin(FILE *of)
{
	fprintf(of, "</tr>\n");
}

// Called after html_file (after printing additional columns)
static void
html_file_record_end(FILE *of)
{
	fprintf(of, "</tr>\n");
}

// Called at the end
static void
html_file_end(FILE *of)
{
	fprintf(of, "</table>\n");
}

// Display a filename of an html file
static void
html_file(FILE *of, Fileid fi)
{
	if (!Option::fname_in_context->get()) {
		fprintf(of, "\n<tr><td></td><td><a href=\"file.html?id=%u\">%s</a></td>",
			fi.get_id(),
			fi.get_path().c_str());
		return;
	}

	// Split path into dir and fname
	string s(fi.get_path());
	string::size_type k = s.find_last_of("/\\");
	if (k == string::npos)
		k = 0;
	else
		k++;
	string dir(s, 0, k);
	string fname(s, k);

	fprintf(of, "<tr><td align=\"right\">%s\n</td>\n", dir.c_str());
	fprintf(of, "<td><a href=\"file.html?id=%u\">%s</a></td>",
		fi.get_id(),
		fname.c_str());
}

// File query page
static int
filequery_page(FILE *of,  void *)
{
	html_head(of, "filequery", "File Query");
	fputs("<FORM ACTION=\"xfilequery.html\" METHOD=\"GET\">\n"
	"<input type=\"checkbox\" name=\"writable\" value=\"1\">Writable<br>\n"
	"<input type=\"checkbox\" name=\"ro\" value=\"1\">Read-only<br>\n", of);
	MQuery<FileMetrics, Fileid &>::metrics_query_form(of);
	fputs("<p>"
	"<input type=\"radio\" name=\"match\" value=\"Y\" CHECKED>Match any of the above\n"
	"&nbsp; &nbsp; &nbsp; &nbsp;\n"
	"<input type=\"radio\" name=\"match\" value=\"L\">Match all of the above\n"
	"<br><hr>\n"
	"File names should "
	"(<input type=\"checkbox\" name=\"xfre\" value=\"1\"> not) \n"
	" match RE\n"
	"<INPUT TYPE=\"text\" NAME=\"fre\" SIZE=20 MAXLENGTH=256>\n"
	"<hr>\n"
	"<p>Query title <INPUT TYPE=\"text\" NAME=\"n\" SIZE=60 MAXLENGTH=256>\n"
	"&nbsp;&nbsp;<INPUT TYPE=\"submit\" NAME=\"qf\" VALUE=\"Show files\">\n"
	"</FORM>\n"
	, of);
	html_tail(of);
	return 0;
}

// Process a file query
static int
xfilequery_page(FILE *of,  void *)
{
	Timer timer;
	char *qname = swill_getvar("n");
	FileQuery query(of, Option::file_icase->get(), current_project);

	if (!query.is_valid())
		return 0;

	multiset <Fileid, FileQuery::FileComparator> sorted_files(query.get_comparator());

	html_head(of, "xfilequery", (qname && *qname) ? qname : "File Query Results");

	for (auto file : engine.get_files()) {
		if (query.eval(file))
			sorted_files.insert(file);
	}
	html_file_begin(of);
	if (modification_state != ms_subst && !browse_only)
		fprintf(of, "<th></th>\n");
	if (query.get_sort_order() != -1)
		fprintf(of, "<th>%s</th>\n", Metrics::get_name<FileMetrics>(query.get_sort_order()).c_str());
	Pager pager(of, Option::entries_per_page->get(), query.base_url(), query.bookmarkable());
	html_file_set_begin(of);
	for (multiset <Fileid, FileQuery::FileComparator>::iterator i = sorted_files.begin(); i != sorted_files.end(); i++) {
		Fileid f = *i;
		if (current_project && !Filedetails::get_attribute(f, current_project))
			continue;
		if (pager.show_next()) {
			html_file(of, *i);
			if (modification_state != ms_subst && !browse_only)
				fprintf(of, "<td><a href=\"fedit.html?id=%u\">edit</a></td>",
				i->get_id());
			if (query.get_sort_order() != -1)
				fprintf(of, "<td align=\"right\">%g</td>", Filedetails::get_pre_cpp_const_metrics(*i).get_metric(query.get_sort_order()));
			html_file_record_end(of);
		}
	}
	html_file_end(of);
	pager.end();
	timer.print_elapsed(of);
	html_tail(of);
	return 0;
}


/*
 * Display the sorted identifiers or functions, taking into account the reverse sort property
 * for properly aligning the output.
 */
template <typename container>
static void
display_sorted(FILE *of, const Query &query, const container &sorted_ids)
{
	if (Option::sort_rev->get())
		fputs("<table><tr><td width=\"50%\" align=\"right\">\n", of);
	else
		fputs("<p>\n", of);

	Pager pager(of, Option::entries_per_page->get(), query.base_url() + "&qi=1", query.bookmarkable());
	typename container::const_iterator i;
	for (i = sorted_ids.begin(); i != sorted_ids.end(); i++) {
		if (pager.show_next()) {
			html(of, **i);
			fputs("<br>\n", of);
		}
	}

	if (Option::sort_rev->get())
		fputs("</td> <td width=\"50%\"> </td></tr></table>\n", of);
	else
		fputs("</p>\n", of);
	pager.end();
}

/*
 * Display the sorted functions with their metrics,
 * taking into account the reverse sort property
 * for properly aligning the output.
 */
static void
display_sorted_function_metrics(FILE *of, const FunQuery &query, const Sfuns &sorted_ids)
{
	fprintf(of, "<table class=\"metrics\"><tr>"
	    "<th width='50%%' align='left'>Name</th>"
	    "<th width='50%%' align='right'>%s</th>\n",
	    Metrics::get_name<FunMetrics>(query.get_sort_order()).c_str());

	Pager pager(of, Option::entries_per_page->get(), query.base_url() + "&qi=1", query.bookmarkable());
	for (Sfuns::const_iterator i = sorted_ids.begin(); i != sorted_ids.end(); i++) {
		if (pager.show_next()) {
			fputs("<tr><td witdh='50%'>", of);
			html(of, **i);
			fprintf(of, "</td><td witdh='50%%' align='right'>%g</td></tr>\n",
			    (*i)->get_pre_cpp_const_metrics().get_metric(query.get_sort_order()));
		}
	}
	fputs("</table>\n", of);
	pager.end();
}


// Identifier query page
static int
iquery_page(FILE *of,  void *)
{
	html_head(of, "iquery", "Identifier Query");
	fputs("<FORM ACTION=\"xiquery.html\" METHOD=\"GET\">\n"
	"<input type=\"checkbox\" name=\"writable\" value=\"1\">Writable<br>\n", of);
	for (int i = attr_begin; i < attr_end; i++)
		fprintf(of, "<input type=\"checkbox\" name=\"a%d\" value=\"1\">%s<br>\n", i,
			Attributes::name(i).c_str());
	fputs(
	"<input type=\"checkbox\" name=\"xfile\" value=\"1\">Crosses file boundary<br>\n"
	"<input type=\"checkbox\" name=\"unused\" value=\"1\">Unused<br>\n"
	"<p>\n"
	"<input type=\"radio\" name=\"match\" value=\"Y\" CHECKED>Match any marked\n"
	"&nbsp; &nbsp; &nbsp; &nbsp;\n"
	"<input type=\"radio\" name=\"match\" value=\"L\">Match all marked\n"
	"&nbsp; &nbsp; &nbsp; &nbsp;\n"
	"<input type=\"radio\" name=\"match\" value=\"E\">Exclude marked\n"
	"&nbsp; &nbsp; &nbsp; &nbsp;\n"
	"<input type=\"radio\" name=\"match\" value=\"T\" >Exact match\n"
	"<br><hr>\n"
	"<table>\n"
	"<tr><td>\n"
	"Identifier names should "
	"(<input type=\"checkbox\" name=\"xire\" value=\"1\"> not) \n"
	" match RE\n"
	"</td><td>\n"
	"<INPUT TYPE=\"text\" NAME=\"ire\" SIZE=20 MAXLENGTH=256>\n"
	"</td></tr>\n"
	"<tr><td>\n"
	"Select identifiers from filenames "
	"(<input type=\"checkbox\" name=\"xfre\" value=\"1\"> not) \n"
	" matching RE\n"
	"</td><td>\n"
	"<INPUT TYPE=\"text\" NAME=\"fre\" SIZE=20 MAXLENGTH=256>\n"
	"</td></tr>\n"
	"</table>\n"
	"<hr>\n"
	"<p>Query title <INPUT TYPE=\"text\" NAME=\"n\" SIZE=60 MAXLENGTH=256>\n"
	"&nbsp;&nbsp;<INPUT TYPE=\"submit\" NAME=\"qi\" VALUE=\"Show identifiers\">\n"
	"<INPUT TYPE=\"submit\" NAME=\"qf\" VALUE=\"Show files\">\n"
	"<INPUT TYPE=\"submit\" NAME=\"qfun\" VALUE=\"Show functions\">\n"
	"</FORM>\n"
	, of);
	html_tail(of);
	return 0;
}

// Function query page
static int
funquery_page(FILE *of,  void *)
{
	html_head(of, "funquery", "Function Query");
	fputs("<FORM ACTION=\"xfunquery.html\" METHOD=\"GET\">\n"
	"<input type=\"checkbox\" name=\"cfun\" value=\"1\">C function<br>\n"
	"<input type=\"checkbox\" name=\"macro\" value=\"1\">Function-like macro<br>\n"
	"<input type=\"checkbox\" name=\"writable\" value=\"1\">Writable declaration<br>\n"
	"<input type=\"checkbox\" name=\"ro\" value=\"1\">Read-only declaration<br>\n"
	"<input type=\"checkbox\" name=\"pscope\" value=\"1\">Project scope<br>\n"
	"<input type=\"checkbox\" name=\"fscope\" value=\"1\">File scope<br>\n"
	"<input type=\"checkbox\" name=\"defined\" value=\"1\">Defined<br>\n", of);
	MQuery<FunMetrics, Call &>::metrics_query_form(of);
	fputs("<p>"
	"<input type=\"radio\" name=\"match\" value=\"Y\" CHECKED>Match any marked\n"
	"&nbsp; &nbsp; &nbsp; &nbsp;\n"
	"<input type=\"radio\" name=\"match\" value=\"L\">Match all marked\n"
	"&nbsp; &nbsp; &nbsp; &nbsp;\n"
	"<input type=\"radio\" name=\"match\" value=\"E\">Exclude marked\n"
	"&nbsp; &nbsp; &nbsp; &nbsp;\n"
	"<input type=\"radio\" name=\"match\" value=\"T\" >Exact match\n"
	"<br><hr>\n"
	"<table>\n"

	"<tr><td>\n"
	"Number of direct callers\n"
	"<select name=\"ncallerop\" value=\"1\">\n",
	of);
	Query::equality_selection(of);
	fputs(
	"</td><td>\n"
	"<INPUT TYPE=\"text\" NAME=\"ncallers\" SIZE=5 MAXLENGTH=10>\n"
	"</td><td>\n"

	"<tr><td>\n"
	"Function names should "
	"(<input type=\"checkbox\" name=\"xfnre\" value=\"1\"> not) \n"
	" match RE\n"
	"</td><td>\n"
	"<INPUT TYPE=\"text\" NAME=\"fnre\" SIZE=20 MAXLENGTH=256>\n"
	"</td></tr>\n"

	"<tr><td>\n"
	"Names of calling functions should "
	"(<input type=\"checkbox\" name=\"xfure\" value=\"1\"> not) \n"
	" match RE\n"
	"</td><td>\n"
	"<INPUT TYPE=\"text\" NAME=\"fure\" SIZE=20 MAXLENGTH=256>\n"
	"</td></tr>\n"

	"<tr><td>\n"
	"Names of called functions should "
	"(<input type=\"checkbox\" name=\"xfdre\" value=\"1\"> not) \n"
	" match RE\n"
	"</td><td>\n"
	"<INPUT TYPE=\"text\" NAME=\"fdre\" SIZE=20 MAXLENGTH=256>\n"
	"</td></tr>\n"

	"<tr><td>\n"
	"Select functions from filenames "
	"(<input type=\"checkbox\" name=\"xfre\" value=\"1\"> not) \n"
	" matching RE\n"
	"</td><td>\n"
	"<INPUT TYPE=\"text\" NAME=\"fre\" SIZE=20 MAXLENGTH=256>\n"
	"</td></tr>\n"
	"</table>\n"
	"<hr>\n"
	"<p>Query title <INPUT TYPE=\"text\" NAME=\"n\" SIZE=60 MAXLENGTH=256>\n"
	"&nbsp;&nbsp;<INPUT TYPE=\"submit\" NAME=\"qi\" VALUE=\"Show functions\">\n"
	"<INPUT TYPE=\"submit\" NAME=\"qf\" VALUE=\"Show files\">\n"
	"</FORM>\n"
	, of);
	html_tail(of);
	return 0;
}

void
display_files(FILE *of, const Query &query, const IFSet &sorted_files)
{
	const string query_url(query.param_url());

	fputs("<h2>Matching Files</h2>\n", of);
	html_file_begin(of);
	html_file_set_begin(of);
	Pager pager(of, Option::entries_per_page->get(), query.base_url() + "&qf=1", query.bookmarkable());
	for (IFSet::iterator i = sorted_files.begin(); i != sorted_files.end(); i++) {
		Fileid f = *i;
		if (current_project && !Filedetails::get_attribute(f, current_project))
			continue;
		if (pager.show_next()) {
			html_file(of, *i);
			fprintf(of, "<td><a href=\"qsrc.html?id=%u&%s\">marked source</a></td>",
				f.get_id(),
				query_url.c_str());
			if (modification_state != ms_subst && !browse_only)
				fprintf(of, "<td><a href=\"fedit.html?id=%u\">edit</a></td>",
				f.get_id());
			html_file_record_end(of);
		}
	}
	html_file_end(of);
	pager.end();
}

// Process an identifier query
static int
xiquery_page(FILE *of,  void *)
{
	Timer timer;
	prohibit_remote_access(of);

	Sids sorted_ids;
	IFSet sorted_files;
	set <Call *> funs;
	bool q_id = !!swill_getvar("qi");	// Show matching identifiers
	bool q_file = !!swill_getvar("qf");	// Show matching files
	bool q_fun = !!swill_getvar("qfun");	// Show matching functions
	char *qname = swill_getvar("n");
	IdQuery query(of, Option::file_icase->get(), current_project);

	if (!query.is_valid()) {
		html_tail(of);
		return 0;
	}

	html_head(of, "xiquery", (qname && *qname) ? qname : "Identifier Query Results");
	if (!opts.is_quiet())
	    cerr << "Evaluating identifier query" << endl;
	for (IdProp::iterator i = ids.begin(); i != ids.end(); i++) {
		progress(i, ids);
		if (!query.eval(*i))
			continue;
		if (q_id)
			sorted_ids.insert(&*i);
		else if (q_file) {
			IFSet f = i->first->sorted_files();
			sorted_files.insert(f.begin(), f.end());
		} else if (q_fun) {
			set <Call *> ecfuns(i->first->functions());
			funs.insert(ecfuns.begin(), ecfuns.end());
		}
	}
	if (!opts.is_quiet())
	    cerr << endl;
	if (q_id) {
		fputs("<h2>Matching Identifiers</h2>\n", of);
		display_sorted(of, query, sorted_ids);
	}
	if (q_file)
		display_files(of, query, sorted_files);
	if (q_fun) {
		fputs("<h2>Matching Functions</h2>\n", of);
		Sfuns sorted_funs([](const Call *a, const Call *b) {
			return Query::string_bi_compare(a->get_name(), b->get_name());
		});
		sorted_funs.insert(funs.begin(), funs.end());
		display_sorted(of, query, sorted_funs);
	}

	timer.print_elapsed(of);
	html_tail(of);
	return 0;
}

// Process a function query
static int
xfunquery_page(FILE *of,  void *)
{
	prohibit_remote_access(of);
	Timer timer;

	IFSet sorted_files;
	bool q_id = !!swill_getvar("qi");	// Show matching identifiers
	bool q_file = !!swill_getvar("qf");	// Show matching files
	char *qname = swill_getvar("n");
	FunQuery query(of, Option::file_icase->get(), current_project);
	Sfuns sorted_funs(query.get_comparator());

	if (!query.is_valid())
		return 0;

	html_head(of, "xfunquery", (qname && *qname) ? qname : "Function Query Results");
	if (!opts.is_quiet())
	    cerr << "Evaluating function query" << endl;
	for (Call::const_fmap_iterator_type i = Call::fbegin(); i != Call::fend(); i++) {
		progress(i, Call::functions());
		if (!query.eval(i->second))
			continue;
		if (q_id)
			sorted_funs.insert(i->second);
		if (q_file)
			sorted_files.insert(i->second->get_fileid());
	}
	if (!opts.is_quiet())
	    cerr << endl;
	if (q_id) {
		fputs("<h2>Matching Functions</h2>\n", of);
		if (query.get_sort_order() != -1)
			display_sorted_function_metrics(of, query, sorted_funs);
		else
			display_sorted(of, query, sorted_funs);
	}
	if (q_file)
		display_files(of, query, sorted_files);
	timer.print_elapsed(of);
	html_tail(of);
	return 0;
}

// Display an identifier property
static void
show_id_prop(FILE *fo, const string &name, bool val)
{
	if (!Option::show_true->get() || val)
		fprintf(fo, ("<li>" + name + ": %s</li>\n").c_str(), val ? "Yes" : "No");
}

// Display whether a macro can be replaced by a C constant
static void
show_c_const(FILE *fo, Eclass *e)
{
	bool val = !e->get_attribute(is_fun_macro)
		&& !e->get_attribute(is_cpp_const)
		&& !e->get_attribute(is_cpp_str_val)
		&& !e->get_attribute(is_undefed_macro)
		&& !e->get_attribute(is_redefined_same_macro)
		&& !e->get_attribute(is_redefined_diff_macro)
		&& ((e->get_attribute(is_def_c_const)
			    && !e->get_attribute(is_def_not_c_const))
		    || (e->get_attribute(is_exp_c_const)
			    && !e->get_attribute(is_exp_not_c_const))
		   );
	fprintf(fo, "<li>Can be replaced by C enumerator: %s\n",
	    val ? "Probably" : "No");
	fprintf(fo, "<li>Can be replaced by C <code>const</code>: %s\n", val && !e->get_attribute(is_used_in_ice) ? "Probably" : "No");
	fprintf(fo, "<ul>\n");
	for (int i = is_fun_macro; i <= is_exp_not_c_const; i++)
		show_id_prop(fo, Attributes::name(i), e->get_attribute(i));
	fprintf(fo, "</ul></li>\n");
}

// Details for each identifier
static int
identifier_page(FILE *fo, void *)
{
	Eclass *e;
	if (!swill_getargs("p(id)", &e)) {
		fprintf(fo, "Missing value");
		return 0;
	}
	char *subst;
	Identifier &id = ids[e];
	if ((subst = swill_getvar("sname"))) {
		if (modification_state == ms_hand_edit) {
			change_prohibited(fo);
			return 0;
		}
		prohibit_browsers(fo);
		prohibit_remote_access(fo);

		// Passing subst directly core-dumps under
		// gcc version 2.95.4 20020320 [FreeBSD 4.7]
		string ssubst(subst);
		id.set_newid(ssubst);
		modification_state = ms_subst;
	}
	html_head(fo, "id", string("Identifier: ") + html(id.get_id()));
	fprintf(fo, "<FORM ACTION=\"id.html\" METHOD=\"GET\">\n<ul>\n");
	for (int i = attr_begin; i < attr_end; i++)
		show_id_prop(fo, Attributes::name(i), e->get_attribute(i));
	show_id_prop(fo, "Crosses file boundary", id.get_xfile());
	show_id_prop(fo, "Unused", e->is_unused());
	if (e->get_attribute(is_macro))
		show_c_const(fo, e);
	fprintf(fo, "<li> Matches %d occurence(s)\n", e->get_size());
	if (Option::show_projects->get()) {
		fprintf(fo, "<li> Appears in project(s): \n<ul>\n");
		if (DP()) {
			cout << "First project " << attr_end << endl;
			cout << "Last project " <<  Attributes::get_num_attributes() - 1 << endl;
		}
		for (Attributes::size_type j = attr_end; j < Attributes::get_num_attributes(); j++)
			if (e->get_attribute(j))
				fprintf(fo, "<li>%s\n", Project::get_projname(j).c_str());
		fprintf(fo, "</ul>\n");
	}
	fprintf(fo, "<li><a href=\"xiquery.html?ec=%p&n=Dependent+Files+for+Identifier+%s&qf=1\">Dependent files</a>", (void *)e, id.get_id().c_str());
	fprintf(fo, "<li><a href=\"xfunquery.html?ec=%p&qi=1&n=Functions+Containing+Identifier+%s\">Associated functions</a>", (void *)e, id.get_id().c_str());
	if (e->get_attribute(is_cfunction) || e->get_attribute(is_macro)) {
		bool found = false;
		// Loop through all declared functions
		for (Call::const_fmap_iterator_type i = Call::fbegin(); i != Call::fend(); i++) {
			if (i->second->contains(e)) {
				if (!found) {
					fprintf(fo, "<li> The identifier occurs (wholy or in part) in function name(s): \n<ol>\n");
					found = true;
				}
				fprintf(fo, "\n<li>");
				html_string(fo, i->second);
				fprintf(fo, " &mdash; <a href=\"fun.html?f=%p\">function page</a>", (void *)i->second);
			}
		}
		if (found)
			fprintf(fo, "</ol><br />\n");
	}

	if ((!e->get_attribute(is_readonly) || Option::rename_override_ro->get()) &&
	    modification_state != ms_hand_edit &&
	    !browse_only) {
		fprintf(fo, "<li> Substitute with: \n"
			"<INPUT TYPE=\"text\" NAME=\"sname\" VALUE=\"%s\" SIZE=10 MAXLENGTH=256> "
			"<INPUT TYPE=\"submit\" NAME=\"repl\" VALUE=\"Save\">\n",
			(id.get_replaced() ? id.get_newid() : id.get_id()).c_str());
		fprintf(fo, "<INPUT TYPE=\"hidden\" NAME=\"id\" VALUE=\"%p\">\n", (void *)e);
		if (!id.get_active())
			fputs("<br>(This substitution is inactive.  Visit the <a href='replacements.html'>replacements page</a> to activate it again.)", fo);
	}
	fprintf(fo, "</ul>\n");
	fprintf(fo, "</FORM>\n");
	html_tail(fo);
	return 0;
}

// Details for each function
static int
function_page(FILE *fo, void *)
{
	Call *f;
	if (!swill_getargs("p(f)", &f)) {
		fprintf(fo, "Missing value");
		return 0;
	}
	char *subst;
	if ((subst = swill_getvar("ncall"))) {
		string ssubst(subst);
		const char *error;
		if (!is_function_call_replacement_valid(ssubst.begin(), ssubst.end(), &error)) {
			fprintf(fo, "Invalid function call refactoring template: %s", error);
			return 0;
		}
		Eclass *ec;
		if (!swill_getargs("p(id)", &ec)) {
			fprintf(fo, "Missing value");
			return 0;
		}
		if (modification_state == ms_hand_edit) {
			change_prohibited(fo);
			return 0;
		}
		prohibit_browsers(fo);
		prohibit_remote_access(fo);
		RefFunCall::store.insert(RefFunCall::store_type::value_type(ec, RefFunCall(f, subst)));
		modification_state = ms_subst;
	}
	html_head(fo, "fun", string("Function: ") + html(f->get_name()) + " (" + f->entity_type_name() + ')');
	fprintf(fo, "<FORM ACTION=\"fun.html\" METHOD=\"GET\">\n");
	fprintf(fo, "<h2>Details</h2>\n");
	fprintf(fo, "<ul>\n");
	fprintf(fo, "<li> Associated identifier(s): ");
	html_string(fo, f);
	Tokid t = f->get_tokid();
	if (f->is_declared()) {
		fprintf(fo, "\n<li> Declared in file <a href=\"file.html?id=%u\">%s</a>",
			t.get_fileid().get_id(),
			t.get_fileid().get_path().c_str());
		int lnum = Filedetails::get_line_number(t.get_fileid(), t.get_streampos());
		fprintf(fo, " <a href=\"src.html?id=%u#%d\">line %d</a><br />(and possibly in other places)\n",
			t.get_fileid().get_id(), lnum, lnum);
			fprintf(fo, " &mdash; <a href=\"qsrc.html?qt=fun&id=%u&match=Y&call=%p&n=Declaration+of+%s\">marked source</a>",
				t.get_fileid().get_id(),
				(void *)f, f->get_name().c_str());
			if (modification_state != ms_subst && !browse_only)
				fprintf(fo, " &mdash; <a href=\"fedit.html?id=%u&re=%s\">edit</a>",
				t.get_fileid().get_id(), f->get_name().c_str());
	}
	if (f->is_defined()) {
		t = f->get_definition();
		fprintf(fo, "<li> Defined in file <a href=\"file.html?id=%u\">%s</a>",
			t.get_fileid().get_id(),
			t.get_fileid().get_path().c_str());
		int lnum = Filedetails::get_line_number(t.get_fileid(), t.get_streampos());
		fprintf(fo, " <a href=\"src.html?id=%u#%d\">line %d</a>\n",
			t.get_fileid().get_id(), lnum, lnum);
		if (modification_state != ms_subst && !browse_only)
			fprintf(fo, " &mdash; <a href=\"fedit.html?id=%u&re=%s\">edit</a>",
			t.get_fileid().get_id(), f->get_name().c_str());
	} else
		fprintf(fo, "<li> No definition found\n");
	// Functions that are Down from us in the call graph
	fprintf(fo, "<li> Calls directly %d functions", f->get_num_call());
	fprintf(fo, "<li> <a href=\"funlist.html?f=%p&n=d&e=1\">Explore directly called functions</a>\n", (void *)f);
	fprintf(fo, "<li> <a href=\"funlist.html?f=%p&n=D\">List of all called functions</a>\n", (void *)f);
	fprintf(fo, "<li> <a href=\"cgraph%s?all=1&f=%p&n=D\">Call graph of all called functions</a>", graph_suffix(), (void *)f);
	// Functions that are Up from us in the call graph
	fprintf(fo, "<li> Called directly by %d functions", f->get_num_caller());
	fprintf(fo, "<li> <a href=\"funlist.html?f=%p&n=u&e=1\">Explore direct callers</a>\n", (void *)f);
	fprintf(fo, "<li> <a href=\"funlist.html?f=%p&n=U\">List of all callers</a>\n", (void *)f);
	fprintf(fo, "<li> <a href=\"cgraph%s?all=1&f=%p&n=U\">Call graph of all callers</a>", graph_suffix(), (void *)f);
	fprintf(fo, "<li> <a href=\"cgraph%s?all=1&f=%p&n=B\">Call graph of all calling and called functions</a> (function in context)", graph_suffix(), (void *)f);

	// Allow function call refactoring only if there is a one to one relationship between the identifier and the function
	Eclass *ec;
	if (f->get_token().get_parts_size() == 1 &&
	    modification_state != ms_hand_edit &&
	    !browse_only &&
	    (ec = f->get_token().get_parts_begin()->get_tokid().check_ec()) &&
	    (!ec->get_attribute(is_readonly) || Option::refactor_fun_arg_override_ro->get())
	    ) {
		// Count associated declared functions
		int nfun = 0;
		for (Call::const_fmap_iterator_type i = Call::fbegin(); i != Call::fend(); i++)
			if (i->second->contains(ec))
				nfun++;
		if (nfun == 1) {
			ostringstream repl_temp;		// Replacement template
			RefFunCall::store_type::const_iterator rfc;
		    	if ((rfc = RefFunCall::store.find(ec)) != RefFunCall::store.end())
				repl_temp << html(rfc->second.get_replacement());
			else if (f->is_defined()) {
				int nparam = f->is_cfun()
					? f->get_post_cpp_metrics().get_metric(FunMetrics::em_nfparam)
					: f->get_pre_cpp_metrics().get_metric(FunMetrics::em_nmparam);
				for (int i = 0; i < nparam; i++) {
					repl_temp << '@' << i + 1;
					if (i + 1 < nparam)
						repl_temp << ", ";
				}
			}
			fprintf(fo, "<li> Refactor arguments into: \n"
				"<INPUT TYPE=\"text\" NAME=\"ncall\" VALUE=\"%s\" SIZE=40 MAXLENGTH=256> "
				"<INPUT TYPE=\"submit\" NAME=\"repl\" VALUE=\"Save\">\n",
				repl_temp.str().c_str());
			fprintf(fo, "<INPUT TYPE=\"hidden\" NAME=\"id\" VALUE=\"%p\">\n", (void *)ec);
			fprintf(fo, "<INPUT TYPE=\"hidden\" NAME=\"f\" VALUE=\"%p\">\n", (void *)f);
			if (rfc != RefFunCall::store.end() && !rfc->second.is_active())
				fputs("<br>(This refactoring is inactive.  Visit the <a href='funargrefs.html'>refactorings page</a> to activate it again.)", fo);
		}
	}
	fprintf(fo, "</ul>\n");
	if (f->is_defined()) {
		// Metrics
		fprintf(fo, "<h2>Metrics</h2>\n<table class='metrics'>\n<tr>"
		    "<th>Metric</th>"
		    "<th>Pre-cpp Value</th>"
		    "<th>Post-cpp Value</th>"
		    "</tr>\n");
		for (int j = 0; j < FunMetrics::metric_max; j++) {
			if (Metrics::is_internal<FunMetrics>(j))
				continue;
			fprintf(fo, "<tr><td>%s</td>",
			    Metrics::get_name<FunMetrics>(j).c_str());
			if (Metrics::is_pre_cpp<FunMetrics>(j))
				fprintf(fo, "<td align='right'>%g</td>",
				    f->get_pre_cpp_metrics().get_metric(j));
			else
				fprintf(fo, "<td align='right'>&mdash;</td>");
			if (Metrics::is_post_cpp<FunMetrics>(j))
				fprintf(fo, "<td align='right'>%g</td></tr>",
				    f->get_post_cpp_metrics().get_metric(j));
			else
				fprintf(fo, "<td align='right'>&mdash;</td></tr>");
		}
		fprintf(fo, "</table>\n");
	}
	fprintf(fo, "</FORM>\n");
	html_tail(fo);
	return 0;
}

/*
 * Visit all functions associated with a call/caller relationship with f
 * Call_path is an HTML string to print next to each function that
 * leads to a page showing the function's call path.  A single %p
 * in the string is used as a placeholder to fill the function's address.
 * The methods to obtain the relationship containers are passed through
 * the fbegin and fend method pointers.
 * If recurse is true the list will also contain all correspondingly
 * associated children functions.
 * If show is true, then a function hyperlink is printed, otherwise
 * only the visited flag is set to visit_id.
 */
static void
visit_functions(FILE *fo, const char *call_path, Call *f,
	Call::const_fiterator_type (Call::*fbegin)() const,
	Call::const_fiterator_type (Call::*fend)() const,
	bool recurse, bool show, int level, int visit_id = 1)
{
	if (level == 0)
		return;

	Call::const_fiterator_type i;

	f->set_visited(visit_id);
	for (i = (f->*fbegin)(); i != (f->*fend)(); i++) {
		if (show && (!(*i)->is_visited(visit_id) || *i == f)) {
			fprintf(fo, "<li> ");
			html(fo, **i);
			if (recurse && call_path)
				fprintf(fo, call_path, *i);
		}
		if (recurse && !(*i)->is_visited(visit_id))
			visit_functions(fo, call_path, *i, fbegin, fend, recurse, show, level - 1, visit_id);
	}
}

/*
 * Visit all files associated with a includes/included relationship with f
 * The method to obtain the relationship container is passed through
 * the get_map function pointer.
 * The method to check if a file should be included in the visit is passed through the
 * the is_ok method pointer.
 * Set the visited flag for all nodes visited.
 */
static void
visit_include_files(Fileid f, const FileIncMap & (*get_map)(Fileid fi),
    bool (IncDetails::*is_ok)() const, int level)
{
	if (level == 0)
		return;

	if (DP())
		cout << "Visiting " << f.get_fname() << endl;
	Filedetails::set_visited(f);
	const FileIncMap &m = get_map(f);
	for (FileIncMap::const_iterator i = m.begin(); i != m.end(); i++) {
		if (!Filedetails::is_visited(i->first) && (i->second.*is_ok)())
			visit_include_files(i->first, get_map, is_ok, level - 1);
	}
}

/*
 * Visit all files associated with a global variable def/ref relationship with f
 * The method to obtain the relationship container is passed through
 * the get_fileid_set method pointer.
 * Set the visited flag for all nodes visited.
 */
static void
visit_globobj_files(Fileid f, const Fileidset & (*get_fileid_set)(Fileid fi),
		int level)
{
	if (level == 0)
		return;

	if (DP())
		cout << "Visiting " << f.get_fname() << endl;
	Filedetails::set_visited(f);
	const Fileidset &s = get_fileid_set(f);
	for (Fileidset::const_iterator i = s.begin(); i != s.end(); i++) {
		if (!Filedetails::is_visited(*i))
			visit_globobj_files(*i, get_fileid_set, level - 1);
	}
}

/*
 * Visit all files associated with a function call relationship with f
 * (a control dependency).
 * The methods to obtain the relationship iterators are passed through
 * the abegin and aend method pointers.
 * Set the visited flag for all nodes visited and the edges matrix for
 * the corresponding edges.
 */
static void
visit_fcall_files(Fileid f, Call::const_fiterator_type (Call::*abegin)() const, Call::const_fiterator_type (Call::*aend)() const, int level, EdgeMatrix &edges)
{
	if (level == 0)
		return;

	if (DP())
		cout << "Visiting " << f.get_fname() << endl;
	Filedetails::set_visited(f);
	/*
	 * For every function in this file:
	 * for every function associated with this function
	 * set the edge and visit the corresponding files.
	 */
	for (FCallSet::const_iterator filefun = Filedetails::get_functions(f).begin(); filefun != Filedetails::get_functions(f).end(); filefun++) {
		if (!(*filefun)->is_cfun())
			continue;
		for (Call::const_fiterator_type afun = ((*filefun)->*abegin)(); afun != ((*filefun)->*aend)(); afun++)
			if ((*afun)->is_defined() && (*afun)->is_cfun()) {
				Fileid f2((*afun)->get_definition().get_fileid());
				edges[f.get_id()][f2.get_id()] = true;
				if (!Filedetails::is_visited(f2))
					visit_fcall_files(f2, abegin, aend, level - 1, edges);
			}
	}
}


extern "C" { const char *swill_getquerystring(void); }

/*
 * Print a list of callers or called functions for the given function,
 * recursively expanding functions that the user has specified.
 */
static void
explore_functions(FILE *fo, Call *f,
	Call::const_fiterator_type (Call::*fbegin)() const,
	Call::const_fiterator_type (Call::*fend)() const,
	int level)
{
	Call::const_fiterator_type i;

	for (i = (f->*fbegin)(); i != (f->*fend)(); i++) {
		fprintf(fo, "<div style=\"margin-left: %dem\">", level * 2);
		if (((*i)->*fbegin)() != ((*i)->*fend)()) {
			/* Functions below; create +/- hyperlink. */
			char param[1024];
			snprintf(param, sizeof(param), "f%02d%p", level, (void *)&(**i));
			char *pval = swill_getvar(param);

			if (pval) {
				// Colapse hyperlink
				string nquery(swill_getquerystring());
				string::size_type start = nquery.find(param);
				if (start != string::npos && start > 0)
					// Erase &param=1 (i.e. param + 3 chars)
					nquery.erase(start - 1, strlen(param) + 3);
				fprintf(fo, "<table class=\"box\"> <tr><th><a class=\"plain\" href=\"%s?%s\">&ndash;</a></th><td>",
				    swill_getvar("__uri__"), nquery.c_str());
			} else
				// Expand hyperlink
				fprintf(fo, "<table class=\"box\"> <tr><th><a class=\"plain\" href=\"%s?%s&%s=1\">+</a></th><td>",
				    swill_getvar("__uri__"),
				    swill_getquerystring(), param);
			html(fo, **i);
			fputs("</td></tr></table></div>\n", fo);
			if (pval && *pval == '1')
				explore_functions(fo, *i, fbegin, fend, level + 1);
		} else {
			/* No functions below. Just display the function. */
			fputs("<table class=\"unbox\"> <tr><th></th><td>", fo);
			html(fo, **i);
			fputs("</td></tr></table></div>\n", fo);
		}
	}
}

// List of functions associated with a given one
static int
funlist_page(FILE *fo, void *)
{
	Call *f;
	char buff[256];

	char *ltype = swill_getvar("n");
	if (!swill_getargs("p(f)", &f) || !ltype) {
		fprintf(fo, "Missing value");
		return 0;
	}
	html_head(fo, "funlist", "Function List");
	fprintf(fo, "<h2>Function ");
	html(fo, *f);
	fprintf(fo, "</h2>");
	const char *calltype;
	bool recurse;
	switch (*ltype) {
	case 'u': case 'd':
		calltype = "directly";
		recurse = false;
		break;
	case 'U': case 'D':
		calltype = "all";
		recurse = true;
		break;
	default:
		fprintf(fo, "Illegal value");
		return 0;
	}
	// Pointers to the ...begin and ...end methods
	Call::const_fiterator_type (Call::*fbegin)() const;
	Call::const_fiterator_type (Call::*fend)() const;
	switch (*ltype) {
	default:
	case 'u':
	case 'U':
		fbegin = &Call::caller_begin;
		fend = &Call::caller_end;
		fprintf(fo, "List of %s calling functions\n", calltype);
		snprintf(buff, sizeof(buff), " &mdash; <a href=\"cpath%s?from=%%p&to=%p\">call path from function</a>", graph_suffix(), (void *)f);
		break;
	case 'd':
	case 'D':
		fbegin = &Call::call_begin;
		fend = &Call::call_end;
		fprintf(fo, "List of %s called functions\n", calltype);
		snprintf(buff, sizeof(buff), " &mdash; <a href=\"cpath%s?from=%p&to=%%p\">call path to function</a>", graph_suffix(), (void *)f);
		break;
	}
	if (swill_getvar("e")) {
		fprintf(fo, "<br />\n");
		explore_functions(fo, f, fbegin, fend, 0);
	} else {
		fprintf(fo, "<ul>\n");
		Call::clear_visit_flags();
		visit_functions(fo, buff, f, fbegin, fend, recurse, true, Option::cgraph_depth->get());
		fprintf(fo, "</ul>\n");
	}
	html_tail(fo);
	return 0;
}

// Display the call paths between functions from and to
// This should be called once to generate the nodes and a second time
// to generate the edges
static bool
call_path(GraphDisplay *gd, Call *from, Call *to, bool generate_nodes)
{
	bool ret = false;

	from->set_visited();
	if (DP())
		cout << "From path: from: " << from->get_name() << " to " << to->get_name() << endl;
	int count = 0;
	for (Call::const_fiterator_type i = from->call_begin(); i != from->call_end(); i++)
		if (!(*i)->is_visited() && (*i == to || call_path(gd, *i, to, generate_nodes))) {
			if (generate_nodes) {
				if (!from->is_printed()) {
					gd->node(from);
					from->set_printed();
					if (browse_only && count++ >= MAX_BROWSING_GRAPH_ELEMENTS)
						break;
				}
				if (!(*i)->is_printed()) {
					gd->node(*i);
					(*i)->set_printed();
					if (browse_only && count++ >= MAX_BROWSING_GRAPH_ELEMENTS)
						break;
				}
			} else {
				gd->edge(from, *i);
				if (browse_only && count++ >= MAX_BROWSING_GRAPH_ELEMENTS)
					break;
			}
			ret = true;
		}
	if (DP())
		cout << "Returns " << ret << endl;
	return (ret);
}

// List the call graph from one function to another
static int
cpath_page(GraphDisplay *gd)
{
	Call *from, *to;

	if (!swill_getargs("p(from)", &from)) {
		fprintf(stderr, "Missing from value");
		return 0;
	}
	if (!swill_getargs("p(to)", &to)) {
		fprintf(stderr, "Missing to value");
		return 0;
	}
	gd->head("callpath", "Function Call Path", Option::cgraph_show->get() == 'e');
	gd->subhead(string("Path ") +
	    function_label(from, true) +
	    string(" &rarr; ") +
	    function_label(to, true));
	Call::clear_print_flags();
	Call::clear_visit_flags();
	call_path(gd, from, to, true);
	Call::clear_visit_flags();
	call_path(gd, from, to, false);
	gd->tail();
	return 0;
}


// Front-end global options page
static int
options_page(FILE *fo, void *)
{
	html_head(fo, "options", "Global Options");
	fprintf(fo, "<FORM ACTION=\"soptions.html\" METHOD=\"GET\">\n");
	Option::display_all(fo);
	fprintf(fo, "<p><p><INPUT TYPE=\"submit\" NAME=\"set\" VALUE=\"OK\">\n");
	fprintf(fo, "<INPUT TYPE=\"submit\" NAME=\"set\" VALUE=\"Cancel\">\n");
	fprintf(fo, "<INPUT TYPE=\"submit\" NAME=\"set\" VALUE=\"Apply\">\n");
	fprintf(fo, "</FORM>\n");
	html_tail(fo);
	return 0;
}

// Front-end global options page
static int
set_options_page(FILE *fo, void *p)
{
	prohibit_remote_access(fo);

	if (string(swill_getvar("set")) == "Cancel") {
		index_page(fo, p);
		return 0;
	}
	Option::set_all();
	if (Option::sfile_re_string->get().length()) {
		string error_msg;
		if (!engine.set_sfile_re(Option::sfile_re_string->get(), error_msg)) {
			html_head(fo, "regerror", "Regular Expression Error");
			fprintf(fo, "<h2>Filename regular expression error</h2>%s", error_msg.c_str());
			html_tail(fo);
			return 0;
		}
	}
	if (string(swill_getvar("set")) == "Apply")
		return options_page(fo, p);
	else
		return index_page(fo, p);
}

// Save options in .cscout/options
static int
save_options_page(FILE *fo, void *)
{
	prohibit_browsers(fo);
	prohibit_remote_access(fo);

	html_head(fo, "save_options", "Options Save");
	ofstream out;
	string fname;
	if (!cscout_output_file("options", out, fname)) {
		html_perror(fo, "Unable to open " + fname + " for writing");
		return 0;
	}
	Option::save_all(out);
	out.close();
	fprintf(fo, "Options have been saved in the file \"%s\".\n", fname.c_str());
	fprintf(fo, "They will be loaded when CScout is executed again.");
	html_tail(fo);
	return 0;
}

// Load the CScout options.
static void
options_load()
{
	ifstream in;
	string fname;

	if (!cscout_input_file("options", in, fname)) {
		fprintf(stderr, "No options file found; will use default options.\n");
		return;
	}
	Option::load_all(in);
	if (Option::sfile_re_string->get().length()) {
		string error_msg;
		if (!engine.set_sfile_re(Option::sfile_re_string->get(), error_msg)) {
			fprintf(stderr, "Filename regular expression error: %s", error_msg.c_str());
			Option::sfile_re_string->erase();
		}
	}
	in.close();
	fprintf(stderr, "Options loaded from %s\n", fname.c_str());
}

static int
file_metrics_page(FILE *fo, void *)
{
	html_head(fo, "filemetrics", "File Metrics");
	ostringstream mstring;
	mstring << file_msum;
	fputs(mstring.str().c_str(), fo);
	html_tail(fo);
	return 0;
}

static int
function_metrics_page(FILE *fo, void *)
{
	html_head(fo, "funmetrics", "Function Metrics");
	ostringstream mstring;
	mstring << fun_msum;
	fputs(mstring.str().c_str(), fo);
	html_tail(fo);
	return 0;
}

static int
id_metrics_page(FILE *fo, void *)
{
	html_head(fo, "idmetrics", "Identifier Metrics");
	ostringstream mstring;
	mstring << id_msum;
	fputs(mstring.str().c_str(), fo);
	html_tail(fo);
	return 0;
}

/*
 * Return true if the call graph is specified for a single function.
 * In this case only show entries that have the visited flag set
 */
static bool
single_function_graph()
{
	Call *f;
	char *ltype = swill_getvar("n");
	if (!swill_getargs("p(f)", &f) || !ltype)
		return false;
	Call::clear_visit_flags();
	// No output, just set the visited flag
	switch (*ltype) {
	case 'D':
		visit_functions(NULL, NULL, f, &Call::call_begin, &Call::call_end, true, false, Option::cgraph_depth->get());
		break;
	case 'U':
		visit_functions(NULL, NULL, f, &Call::caller_begin, &Call::caller_end, true, false, Option::cgraph_depth->get());

		break;
	case 'B':
		visit_functions(NULL, NULL, f, &Call::call_begin, &Call::call_end, true, false, Option::cgraph_depth->get(), 1);
		visit_functions(NULL, NULL, f, &Call::caller_begin, &Call::caller_end, true, false, Option::cgraph_depth->get(), 2);

		break;
	}
	return true;
}

/*
 * Return true if the include/global/call graph is specified for a single file.
 * In this case caller will only show entries that have the visited flag set, so
 * set this flag as specified.
 * For function * call graphs also fill edges with a matrix indicating the dges to draw
 */
static bool
single_file_graph(char gtype, EdgeMatrix &edges)
{
	int id;
	char *ltype = swill_getvar("n");
	if (!swill_getargs("i(f)", &id) || !ltype)
		return false;
	Fileid fileid(id);
	Filedetails::clear_all_visited();
	// No output, just set the visited flag
	switch (gtype) {
	case 'I':		// Include graph
		switch (*ltype) {
		case 'D':
			visit_include_files(fileid, &Filedetails::get_includers, &IncDetails::is_directly_included, Option::cgraph_depth->get());
			break;
		case 'U':
			visit_include_files(fileid, &Filedetails::get_includes, &IncDetails::is_directly_included, Option::cgraph_depth->get());
			break;
		}
		break;
	case 'C':		// Compile-time dependency graph
		switch (*ltype) {
		case 'D':
			visit_include_files(fileid, &Filedetails::get_includers, &IncDetails::is_required, Option::cgraph_depth->get());
			break;
		case 'U':
			visit_include_files(fileid, &Filedetails::get_includes, &IncDetails::is_required, Option::cgraph_depth->get());
			break;
		}
		break;
	case 'G':		// Global object def/ref graph (data dependency)
		switch (*ltype) {
		case 'D':
			visit_globobj_files(fileid, &Filedetails::get_glob_uses, Option::cgraph_depth->get());
			break;
		case 'U':
			visit_globobj_files(fileid, &Filedetails::get_glob_used_by, Option::cgraph_depth->get());
			break;
		}
		break;
	case 'F':		// Function call graph (control dependency)
		int size = Fileid::max_id() + 1;
		edges.insert(edges.begin(), size, vector<bool>(size, 0));
		switch (*ltype) {
		case 'D':
			visit_fcall_files(fileid, &Call::call_begin, &Call::call_end, Option::cgraph_depth->get(), edges);
			break;
		case 'U':
			visit_fcall_files(fileid, &Call::caller_begin, &Call::caller_end, Option::cgraph_depth->get(), edges);
			break;
		}
		break;
	}
	return true;
}

/*
 * Return true if the call graph is specified for functions in a single file.
 * In this case only show entries that have the visited flag set
 */
static bool
single_file_function_graph()
{
	int id;
	if (!swill_getargs("i(fid)", &id))
		return false;
	Fileid fileid(id);

	Call::clear_visit_flags();
	Call::const_fmap_iterator_type fun;
	for (fun = Call::fbegin(); fun != Call::fend(); fun++)
		if (fun->second->get_begin().get_tokid().get_fileid() == fileid)
			fun->second->set_visited();
	return true;
}

// Call graph
static int
cgraph_page(GraphDisplay *gd)
{
	bool all, only_visited;
	if (gd->uses_swill) {
		all = !!swill_getvar("all");
		only_visited = (single_function_graph() || single_file_function_graph());
	}
	else {
		all = gd->all;
		only_visited = gd->only_visited;
	}
	gd->head("cgraph", "Call Graph", Option::cgraph_show->get() == 'e');
	int count = 0;
	// First generate the node labels
	Call::const_fmap_iterator_type fun;
	Call::const_fiterator_type call;
	for (fun = Call::fbegin(); fun != Call::fend(); fun++) {
		if (!all && fun->second->is_file_scoped())
			continue;
		if (only_visited && !fun->second->is_visited())
			continue;
		gd->node(fun->second);
		if (browse_only && count++ >= MAX_BROWSING_GRAPH_ELEMENTS)
			goto end;
	}
	// Now the edges
	for (fun = Call::fbegin(); fun != Call::fend(); fun++) {
		if (!all && fun->second->is_file_scoped())
			continue;
		if (only_visited && !fun->second->is_visited())
			continue;
		for (call = fun->second->call_begin(); call != fun->second->call_end(); call++) {
			if (!all && (*call)->is_file_scoped())
				continue;
			// No edge unless both functions were visited on the same tour
			// as indicated by the corresponding bitmasks.
			if (only_visited && !((*call)->get_visited() & fun->second->get_visited()))
				continue;
			gd->edge(fun->second, *call);
			if (browse_only && count++ >= MAX_BROWSING_GRAPH_ELEMENTS)
				goto end;
		}
	}
end:
	gd->tail();
	return 0;
}

// File dependency graph
static int
fgraph_page(GraphDisplay *gd)
{

	const char *gtype = NULL;
	const char *ltype = NULL;
	if (gd->uses_swill) {
		gtype = swill_getvar("gtype");		// Graph type
		ltype = swill_getvar("n");
	} else {
		gtype = gd->gtype.c_str();
		ltype = gd->ltype.c_str();
	}
	if (!gtype || !*gtype || (*gtype == 'F' && !ltype)) {
		gd->head("fgraph", "Error", false);
		gd->error("Missing value");
		gd->tail();
		return 0;
	}
	bool all, only_visited;
	EdgeMatrix edges;
	bool empty_node = (Option::fgraph_show->get() == 'e');
	if (gd->uses_swill) {
		all = !!swill_getvar("all");		// Otherwise exclude read-only files
		only_visited = single_file_graph(*gtype, edges);
	}
	else {
		all = gd->all;
		only_visited = gd->only_visited;
	}

	switch (*gtype) {
	case 'I':		// Include graph
		gd->head("fgraph", "Include Graph", empty_node);
		break;
	case 'C':		// Compile-time dependency graph
		gd->head("fgraph", "Compile-Time Dependency Graph", empty_node);
		break;
	case 'G':		// Global object def/ref graph (data dependency)
		gd->head("fgraph", "Global Object (Data) Dependency Graph", empty_node);
		break;
	case 'F':		// Function call graph (control dependency)
		gd->head("fgraph", "Function Call (Control) Dependency Graph", empty_node);
		if (!only_visited) {
			int size = Fileid::max_id() + 1;
			edges.insert(edges.begin(), size, vector<bool>(size, 0));
			// Fill the edges for all files
			Filedetails::clear_all_visited();
			for (const auto &file : engine.get_files()) {
				if (Filedetails::is_visited(file))
					continue;
				if (!all && file.get_readonly())
					continue;
				switch (*ltype) {
				case 'D':
					visit_fcall_files(file, &Call::call_begin, &Call::call_end, Option::cgraph_depth->get(), edges);
					break;
				case 'U':
					visit_fcall_files(file, &Call::caller_begin, &Call::caller_end, Option::cgraph_depth->get(), edges);
					break;
				}
			}
		}
		break;
	default:
		gd->head("fgraph", "Error", empty_node);
		gd->error("Unknown graph type");
		gd->tail();
		return 0;
	}
	int count = 0;
	// First generate the node labels
	for (const auto &file : engine.get_files()) {
		if (!all && file.get_readonly())
			continue;
		if (only_visited && !Filedetails::is_visited(file))
			continue;
		gd->node(file);
		if (browse_only && count++ >= MAX_BROWSING_GRAPH_ELEMENTS)
			goto end;
	}
	// Now the edges
	for (const auto &file : engine.get_files()) {
		if (!all && file.get_readonly())
			continue;
		if (only_visited && !Filedetails::is_visited(file))
			continue;
		switch (*gtype) {
		case 'C':		// Compile-time dependency graph
		case 'I': {		// Include graph
			const FileIncMap &m(Filedetails::get_includes(file));
			for (FileIncMap::const_iterator j = m.begin(); j != m.end(); j++) {
				if (*gtype == 'I' && !j->second.is_directly_included())
					continue;
				if (*gtype == 'C' && !j->second.is_required())
					continue;
				if (!all && j->first.get_readonly())
					continue;
				if (only_visited && !Filedetails::is_visited(j->first))
					continue;
				gd->edge(j->first, file);
				if (browse_only && count++ >= MAX_BROWSING_GRAPH_ELEMENTS)
					goto end;
			}
			break;
		}
		case 'F':		// Function call graph (control dependency)
			for (const auto &file2 : engine.get_files()) {				
				if (!all && file2.get_readonly())
					continue;
				if (only_visited && !Filedetails::is_visited(file2))
					continue;
				if (file == file2)
					continue;
				if (DP())
					cout << "Checking " << file.get_fname() << " - " << file2.get_fname() << endl;
				if (edges[file.get_id()][file2.get_id()])
					switch (*ltype) {
					case 'D':
						gd->edge(file2, file);
						break;
					case 'U':
						gd->edge(file, file2);
						break;
					}
				if (browse_only && count++ >= MAX_BROWSING_GRAPH_ELEMENTS)
					goto end;
			}
			break;
		case 'G':		// Global object def/ref graph (data dependency)
			for (Fileidset::const_iterator j = Filedetails::get_glob_uses(file).begin(); j != Filedetails::get_glob_uses(file).end(); j++) {
				if (!all && j->get_readonly())
					continue;
				if (only_visited && !Filedetails::is_visited(*j))
					continue;
				gd->edge(*j, file);
				if (browse_only && count++ >= MAX_BROWSING_GRAPH_ELEMENTS)
					goto end;
			}
			break;
		default:
			csassert(0);
			break;
		}
	}
end:
	gd->tail();
	return 0;
}

// Graph: text
static int
graph_txt_page(FILE *fo, void (*graph_fun)(GraphDisplay *))
{
	// Add output and outfile argument to enable output to outfile
	int output;
	char *outfile;
	if (swill_getargs("i(output)s(outfile)", &output, &outfile) && output && strlen(outfile)) {
		FILE *ofile = fopen(outfile, "w+");
		if (ofile == NULL) {
			html_perror(fo, "Unable to open " + string(outfile) + " for writing");
			return 0;
		}
		GDTxt gdout(ofile);
		graph_fun(&gdout);
		fclose(ofile);
	}

	if (opts.process_mode != CscoutOptions::pm_call_graph) {
		GDTxt gd(fo);
		graph_fun(&gd);
	}
	return 0;
}

// Graph: HTML
static int
graph_html_page(FILE *fo, void (*graph_fun)(GraphDisplay *))
{
	GDHtml gd(fo);
	graph_fun(&gd);
	return 0;
}

// Graph: dot
static int
graph_dot_page(FILE *fo, void (*graph_fun)(GraphDisplay *))
{
	GDDot gd(fo);
	graph_fun(&gd);
	return 0;
}

// Graph: SVG via dot
static int
graph_svg_page(FILE *fo, void (*graph_fun)(GraphDisplay *))
{
	prohibit_remote_access(fo);
	GDSvg gd(fo);
	graph_fun(&gd);
	return 0;
}

// Graph: GIF via dot
static int
graph_gif_page(FILE *fo, void (*graph_fun)(GraphDisplay *))
{
	prohibit_remote_access(fo);
	GDGif gd(fo);
	graph_fun(&gd);
	return 0;
}


// Graph: PNG via dot
static int
graph_png_page(FILE *fo, void (*graph_fun)(GraphDisplay *))
{
	prohibit_remote_access(fo);
	GDPng gd(fo);
	graph_fun(&gd);
	return 0;
}


// Graph: PDF via dot
static int
graph_pdf_page(FILE *fo, void (*graph_fun)(GraphDisplay *))
{
	prohibit_remote_access(fo);
	GDPdf gd(fo);
	graph_fun(&gd);
	return 0;
}


// Produce call graphs with -R option
static void
produce_call_graphs(const vector <string> &call_graphs)
{
	char base_splitter = '?';
	char opts_splitter = '&';
	char opt_spltter = '=';
	GDArgsKeys gdargskeys;

	for (string url: call_graphs) {
		vector<string> split_base_and_opts = split_by_delimiter(url, base_splitter);
		if (split_base_and_opts.size() == 0) {
			cerr << url << "is not a valid url" << endl;
			continue;
		}

		FILE *target = fopen(split_base_and_opts[0].c_str() , "w+");
		if (target == NULL) {
			perror(split_base_and_opts[0].c_str());
			continue;
		}
		string base = split_base_and_opts[0];
		GDTxt gd(target);
		// Disable swill
		gd.uses_swill = false;
		vector<string> opts;

		if (split_base_and_opts.size() != 1) {

			opts = split_by_delimiter(split_base_and_opts[1], opts_splitter);

			// Parse opts
			for (string opt: opts) {
				vector<string> opt_tmp = split_by_delimiter(opt, opt_spltter);
				if (opt_tmp.size() < 2) continue;

				// Key-value pairs
				string key = opt_tmp[0];
				string val = opt_tmp[1];

				if (!key.compare(gdargskeys.ALL)) {
					gd.all = (bool) atoi(val.c_str());
				} else if (!key.compare(gdargskeys.ONLY_VISITED)) {
					gd.only_visited = (bool) atoi(val.c_str());
				} else if (!key.compare(gdargskeys.GTYPE)) {
					gd.gtype = val;
				} else if (!key.compare(gdargskeys.LTYPE)) {
					gd.ltype = val;
					Option::cgraph_show->set_hard(val.c_str());
				} else if (!key.compare("type")) {
					Option::show_function_type->set_hard((bool) atoi(val.c_str()));
				} else if (!key.compare("defined")) {
					Option::is_defined->set_hard((bool) atoi(val.c_str()));
				} else if (!key.compare("nline")) {
					Option::line_num->set_hard((bool) atoi(val.c_str()));
				} else if (!key.compare("nodes")) {
					Option::print_nodes->set_hard((bool) atoi(val.c_str()));
				}

			}

		}

		if (!base.compare(gdargskeys.CGRAPH)) {
			cgraph_page(&gd);
		}
		else if (!base.compare(gdargskeys.FGRAPH)) {
			fgraph_page(&gd);
		}

		fclose(target);
	}



}


// Setup graph handling for all supported graph output types
static void
graph_handle(string name, int (*graph_fun)(GraphDisplay *))
{
	swill_handle((name + ".html").c_str(), graph_html_page, graph_fun);
	swill_handle((name + ".txt").c_str(), graph_txt_page, graph_fun);
	swill_handle((name + "_dot.txt").c_str(), graph_dot_page, graph_fun);
	swill_handle((name + ".svg").c_str(), graph_svg_page, graph_fun);
	swill_handle((name + ".gif").c_str(), graph_gif_page, graph_fun);
	swill_handle((name + ".png").c_str(), graph_png_page, graph_fun);
	swill_handle((name + ".pdf").c_str(), graph_pdf_page, graph_fun);
}

// Display all projects, allowing user to select
static int
select_project_page(FILE *fo, void *)
{
	html_head(fo, "sproject", "Select Active Project");
	fprintf(fo, "<ul>\n");
	fprintf(fo, "<li> <a href=\"setproj.html?projid=0\">All projects</a>\n");
	for (Attributes::size_type j = attr_end; j < Attributes::get_num_attributes(); j++)
		fprintf(fo, "<li> <a href=\"setproj.html?projid=%u\">%s</a>\n", (unsigned)j, Project::get_projname(j).c_str());
	fprintf(fo, "\n</ul>\n");
	html_tail(fo);
	return 0;
}

// Select a single project (or none) to restrict file/identifier results
static int
set_project_page(FILE *fo, void *p)
{
	prohibit_browsers(fo);
	prohibit_remote_access(fo);

	if (!swill_getargs("i(projid)", &current_project)) {
		fprintf(fo, "Missing value");
		return 0;
	}
	index_page(fo, p);
	return 0;
}

// Display information about CScout
static int
about_page(FILE *fo, void *)
{
	html_head(fo, "about", "About CScout");
	fputs(version_info(true).c_str(), fo);
	html_tail(fo);
	return 0;
}


// Index
static int
index_page(FILE *of, void *)
{
	html_head(of, "index", "CScout Main Page", "<img src=\"logo.png\">Scout Main Page");
	fputs(
		"<table><tr><td valign=\"top\">\n"
		"<div class=\"mainblock\">\n"
		"<h2>Files</h2>\n"
		"<ul>\n"
		"<li> <a href=\"filemetrics.html\">File metrics</a>\n"
		"<li>\n", of);
	dir_top(of, "Browse file tree");
	fprintf(of,
		"<li> <a href=\"xfilequery.html?ro=1&writable=1&match=Y&n=All+Files\">All files</a>\n"
		"<li> <a href=\"xfilequery.html?ro=1&match=Y&n=Read-only+Files\">Read-only files</a>\n"
		"<li> <a href=\"xfilequery.html?writable=1&match=Y&n=Writable+Files\">Writable files</a>\n");
	fprintf(of, "<li> <a href=\"xiquery.html?writable=1&a%d=1&unused=1&match=L&qf=1&n=Files+Containing+Unused+Project-scoped+Writable+Identifiers\">Files containing unused project-scoped writable identifiers</a>\n", is_lscope);
	fprintf(of, "<li> <a href=\"xiquery.html?writable=1&a%d=1&unused=1&match=L&qf=1&n=Files+Containing+Unused+File-scoped+Writable+Identifiers\">Files containing unused file-scoped writable identifiers</a>\n", is_cscope);
	fprintf(of, "<li> <a href=\"xfilequery.html?writable=1&c%d=%d&n%d=0&match=L&fre=%%5C.%%5BcC%%5D%%24&n=Writable+.c+Files+Without+Any+Statements\">Writable .c files without any statements</a>\n", FileMetrics::em_nstmt, Query::ec_eq, FileMetrics::em_nstmt);
	fprintf(of, "<li> <a href=\"xfilequery.html?writable=1&order=%d&c%d=%d&n%d=0&reverse=0&match=L&n=Writable+Files+Containing+Unprocessed+Lines\">Writable files containing unprocessed lines</a>\n", Metrics::em_nuline, Metrics::em_nuline, Query::ec_gt, Metrics::em_nuline);
	fprintf(of, "<li> <a href=\"xfilequery.html?writable=1&c%d=%d&n%d=0&match=L&n=Writable+Files+Containing+Strings\">Writable files containing strings</a>\n", Metrics::em_nstring, Query::ec_gt, Metrics::em_nstring);
	fprintf(of, "<li> <a href=\"xfilequery.html?writable=1&c%d=%d&n%d=0&match=L&fre=%%5C.%%5BhH%%5D%%24&n=Writable+.h+Files+With+%%23include+directives\">Writable .h files with #include directives</a>\n", FileMetrics::em_nincfile, Query::ec_gt, FileMetrics::em_nincfile);
	fprintf(of, "<li> <a href=\"filequery.html\">Specify new file query</a>\n"
		"</ul></div>\n");

	fputs(
		"<div class=\"mainblock\">\n"
		"<h2>File Dependencies</h2>"
		"<ul>\n", of);
	fprintf(of, "<li> File include graph: <a href=\"fgraph%s?gtype=I\">writable files</a>, ", graph_suffix());
	fprintf(of, "<a href=\"fgraph%s?gtype=I&all=1\">all files</a>", graph_suffix());
	fprintf(of, "<li> Compile-time dependency graph: <a href=\"fgraph%s?gtype=C\">writable files</a>, ", graph_suffix());
	fprintf(of, "<a href=\"fgraph%s?gtype=C&all=1\">all files</a>", graph_suffix());
	fprintf(of, "<li> Control dependency graph (through function calls): <a href=\"fgraph%s?gtype=F&n=D\">writable files</a>, ", graph_suffix());
	fprintf(of, "<a href=\"fgraph%s?gtype=F&n=D&all=1\">all files</a>", graph_suffix());
	fprintf(of, "<li> Data dependency graph (through global variables): <a href=\"fgraph%s?gtype=G\">writable files</a>, ", graph_suffix());
	fprintf(of, "<a href=\"fgraph%s?gtype=G&all=1\">all files</a>", graph_suffix());
	fputs("</ul></div>", of);

	fputs(
		"<div class=\"mainblock\">\n"
		"<h2>Functions and Macros</h2>\n"
		"<ul>\n"
		"<li> <a href=\"funmetrics.html\">Function metrics</a>\n"
		"<li> <a href=\"xfunquery.html?writable=1&ro=1&match=Y&ncallerop=0&ncallers=&n=All+Functions&qi=x\">All functions</a>\n"
		"<li> <a href=\"xfunquery.html?writable=1&pscope=1&match=L&ncallerop=0&ncallers=&n=Project-scoped+Writable+Functions&qi=x\">Project-scoped writable functions</a>\n"
		"<li> <a href=\"xfunquery.html?writable=1&fscope=1&match=L&ncallerop=0&ncallers=&n=File-scoped+Writable+Functions&qi=x\">File-scoped writable functions</a>\n"
		"<li> <a href=\"xfunquery.html?writable=1&match=Y&ncallerop=1&ncallers=0&n=Writable+Functions+that+Are+Not+Directly+Called&qi=x\">Writable functions that are not directly called</a>\n"
		"<li> <a href=\"xfunquery.html?writable=1&match=Y&ncallerop=1&ncallers=1&n=Writable+Functions+that+Are++Called+Exactly+Once&qi=x\">Writable functions that are called exactly once</a>\n", of);
	fprintf(of, "<li> <a href=\"cgraph%s\">Non-static function call graph</a>", graph_suffix());
	fprintf(of, "<li> <a href=\"cgraph%s?all=1\">Function and macro call graph</a>", graph_suffix());
	fputs("<li> <a href=\"funquery.html\">Specify new function query</a>\n"
		"</ul></div>\n", of);

	fprintf(of, "</td><td valign=\"top\">\n");

	fputs(
		"<div class=\"mainblock\">\n"
		"<h2>Identifiers</h2>\n"
		"<ul>\n"
		"<li> <a href=\"idmetrics.html\">Identifier metrics</a>\n",
		of);
	fprintf(of, "<li> <a href=\"xiquery.html?writable=1&a%d=1&match=Y&qi=1&n=All+Identifiers\">All identifiers</a>\n", is_readonly);
	fprintf(of, "<li> <a href=\"xiquery.html?a%d=1&match=Y&qi=1&n=Read-only+Identifiers\">Read-only identifiers</a>\n", is_readonly);
	fputs("<li> <a href=\"xiquery.html?writable=1&match=Y&qi=1&n=Writable+Identifiers\">Writable identifiers</a>\n"
		"<li> <a href=\"xiquery.html?writable=1&xfile=1&match=L&qi=1&n=File-spanning+Writable+Identifiers\">File-spanning writable identifiers</a>\n", of);
	fprintf(of, "<li> <a href=\"xiquery.html?writable=1&a%d=1&unused=1&match=L&qi=1&n=Unused+Project-scoped+Writable+Identifiers\">Unused project-scoped writable identifiers</a>\n", is_lscope);
	fprintf(of, "<li> <a href=\"xiquery.html?writable=1&a%d=1&unused=1&match=L&qi=1&n=Unused+File-scoped+Writable+Identifiers\">Unused file-scoped writable identifiers</a>\n", is_cscope);
	fprintf(of, "<li> <a href=\"xiquery.html?writable=1&a%d=1&unused=1&match=L&qi=1&n=Unused+Writable+Macros\">Unused writable macros</a>\n", is_macro);
	// xfile is implicitly 0
	fprintf(of, "<li> <a href=\"xiquery.html?writable=1&a%d=1&a%d=1&match=T&ire=&fre=&n=Writable+identifiers+that+should+be+made+static&qi=1\">Writable variable identifiers that should be made static</a>\n", is_ordinary, is_lscope);
	fprintf(of, "<li> <a href=\"xiquery.html?writable=1&a%d=1&a%d=1&a%d=1&match=T&ire=&fre=&n=Writable+identifiers+that+should+be+made+static&qi=1\">Writable function identifiers that should be made static</a>\n", is_ordinary, is_lscope, is_cfunction);
	fprintf(of,
		"<li> <a href=\"iquery.html\">Specify new identifier query</a>\n"
		"</ul></div>"
	);


	if (!browse_only)
		fputs(
			"<div class=\"mainblock\">\n"
			"<h2>Operations</h2>"
			"<ul>\n"
			"<li> <a href=\"options.html\">Global options</a>\n"
			" &mdash; <a href=\"save_options.html\">save global options</a>\n"
			"<li> <a href=\"replacements.html\">Identifier replacements</a>\n"
			"<li> <a href=\"funargrefs.html\">Function argument refactorings</a>\n"
			"<li> <a href=\"sproject.html\">Select active project</a>\n"
			"<li> <a href=\"about.html\">About CScout</a>\n"
			"<li> <a href=\"save.html\">Save changes and continue</a>\n"
			"<li> <a href=\"sexit.html\">Exit &mdash; saving changes</a>\n"
			"<li> <a href=\"qexit.html\">Exit &mdash; ignore changes</a>\n"
			"</ul></div>", of);
	fputs("</td></tr></table>\n", of);
	html_tail(of);
	return 0;
}

static int
file_page(FILE *of, void *)
{
	int id;
	if (!swill_getargs("i(id)", &id)) {
		fprintf(of, "Missing value");
		return 0;
	}
	Fileid i(id);
	const string &pathname = i.get_path();
	html_head(of, "file", string("File: ") + html(pathname));
	fprintf(of, "<h2>Details</h2><ul>\n");
	fprintf(of, "<li> Read-only: %s", i.get_readonly() ? "Yes" : "No");
	if (Option::show_projects->get()) {
		fprintf(of, "\n<li> Used in project(s): \n<ul>");
		for (Attributes::size_type j = attr_end; j < Attributes::get_num_attributes(); j++)
			if (Filedetails::get_attribute(i, j))
				fprintf(of, "<li>%s\n", Project::get_projname(j).c_str());
		fprintf(of, "</ul>\n");
	}
	if (Option::show_identical_files->get()) {
		const set <Fileid> &copies(Filedetails::get_identical_files(i));
		fprintf(of, "<li>Other exact copies:%s\n", copies.size() > 1 ? "<ul>\n" : " (none)");
		for (set <Fileid>::const_iterator j = copies.begin(); j != copies.end(); j++) {
			if (*j != i) {
				fprintf(of, "<li>");
				html_string(of, j->get_path());
			}
		}
		if (copies.size() > 1)
			fprintf(of, "</ul>\n");
	}
	if (Filedetails::is_hand_edited(i))
		fprintf(of, "<li>Hand edited\n");
	fprintf(of, "<li> <a href=\"dir.html?dir=%p\">File's directory</a>", (void *)dir_add_file(i));

	fprintf(of, "</ul>\n<h2>Listings</h2><ul>\n<li> <a href=\"src.html?id=%u\">Source code</a>\n", i.get_id());
	fprintf(of, "<li> <a href=\"src.html?id=%u&marku=1\">Source code with unprocessed regions marked</a>\n", i.get_id());
	fprintf(of, "<li> <a href=\"qsrc.html?qt=id&id=%u&match=Y&writable=1&a%d=1&n=Source+Code+With+Identifier+Hyperlinks\">Source code with identifier hyperlinks</a>\n", i.get_id(), is_readonly);
	fprintf(of, "<li> <a href=\"qsrc.html?qt=id&id=%u&match=L&writable=1&a%d=1&n=Source+Code+With+Hyperlinks+to+Project-global+Writable+Identifiers\">Source code with hyperlinks to project-global writable identifiers</a>\n", i.get_id(), is_lscope);
	fprintf(of, "<li> <a href=\"qsrc.html?qt=id&id=%u&match=L&writable=1&a%d=1&n=Source+Code+With+Hyperlinks+to+File-local+Writable+Identifiers\">Source code with hyperlinks to file-local writable identifiers</a>\n", i.get_id(), is_cscope);
	fprintf(of, "<li> <a href=\"qsrc.html?qt=fun&id=%u&match=Y&writable=1&ro=1&n=Source+Code+With+Hyperlinks+to+Function+and+Macro+Declarations\">Source code with hyperlinks to function and macro declarations</a>\n", i.get_id());
	if (modification_state != ms_subst && !browse_only)
		fprintf(of, "<li> <a href=\"fedit.html?id=%u\">Edit the file</a>", i.get_id());

	fprintf(of, "</ul>\n<h2>Functions</h2><ul>\n");
	fprintf(of, "<li> <a href=\"xfunquery.html?fid=%d&pscope=1&match=L&ncallerop=0&ncallers=&n=Defined+Project-scoped+Functions+in+%s&qi=x\">Defined project-scoped functions</a>\n"
		"<li> <a href=\"xfunquery.html?fid=%d&fscope=1&match=L&ncallerop=0&ncallers=&n=Defined+File-scoped+Functions+in+%s&qi=x\">Defined file-scoped functions</a>\n",
		i.get_id(), i.get_fname().c_str(), i.get_id(), i.get_fname().c_str());
	fprintf(of, "<li> <a href=\"cgraph%s?fid=%d&all=1\">Function and macro call graph</a>", graph_suffix(), i.get_id());

	fprintf(of, "</ul>\n<h2>File Dependencies</h2><ul>\n");
	fprintf(of, "<li> Graph of files that depend on this file at compile time: "
	    "<a href=\"fgraph%s?gtype=C&f=%d&n=D\">writable</a>, "
	    "<a href=\"fgraph%s?gtype=C&all=1&f=%d&n=D\">all</a>",
	    graph_suffix(), i.get_id(), graph_suffix(), i.get_id());
	fprintf(of, "<li> Graph of files on which this file depends at compile time: "
	    "<a href=\"fgraph%s?gtype=C&f=%d&n=U\">writable</a>, "
	    "<a href=\"fgraph%s?gtype=C&all=1&f=%d&n=U\">all</a>",
	    graph_suffix(), i.get_id(), graph_suffix(), i.get_id());
	fprintf(of, "<li> Graph of files whose functions this file calls (control dependency): "
	    "<a href=\"fgraph%s?gtype=F&f=%d&n=D\">writable</a>, "
	    "<a href=\"fgraph%s?gtype=F&all=1&f=%d&n=D\">all</a>",
	    graph_suffix(), i.get_id(), graph_suffix(), i.get_id());
	fprintf(of, "<li> Graph of files calling this file's functions (control dependency): "
	    "<a href=\"fgraph%s?gtype=F&f=%d&n=U\">writable</a>, "
	    "<a href=\"fgraph%s?gtype=F&all=1&f=%d&n=U\">all</a>",
	    graph_suffix(), i.get_id(), graph_suffix(), i.get_id());
	fprintf(of, "<li> Graph of files whose global variables this file accesses (data dependency): "
	    "<a href=\"fgraph%s?gtype=G&f=%d&n=D\">writable</a>, "
	    "<a href=\"fgraph%s?gtype=G&all=1&f=%d&n=D\">all</a>",
	    graph_suffix(), i.get_id(), graph_suffix(), i.get_id());
	fprintf(of, "<li> Graph of files accessing this file's global variables (data dependency): "
	    "<a href=\"fgraph%s?gtype=G&f=%d&n=U\">writable</a>, "
	    "<a href=\"fgraph%s?gtype=G&all=1&f=%d&n=U\">all</a>",
	    graph_suffix(), i.get_id(), graph_suffix(), i.get_id());

	fprintf(of, "</ul>\n<h2>Include Files</h2><ul>\n");
	fprintf(of, "<li> <a href=\"qinc.html?id=%u&direct=1&writable=1&includes=1&n=Directly+Included+Writable+Files\">Writable files that this file directly includes</a>\n", i.get_id());
	fprintf(of, "<li> <a href=\"qinc.html?id=%u&includes=1&n=All+Included+Files\">All files that this file includes</a>\n", i.get_id());
	fprintf(of, "<li> <a href=\"fgraph%s?gtype=I&all=1&f=%d&n=U\">Include graph of all included files</a>", graph_suffix(), i.get_id());
	fprintf(of, "<li> <a href=\"fgraph%s?gtype=I&f=%d&n=U\">Include graph of writable included files</a>", graph_suffix(), i.get_id());
	fprintf(of, "<li> <a href=\"fgraph%s?gtype=I&all=1&f=%d&n=D\">Include graph of all including files</a>", graph_suffix(), i.get_id());
	fprintf(of, "<li> <a href=\"qinc.html?id=%u&includes=1&used=1&writable=1&n=All+Required+Included+Writable+Files\">All writable files that this file must include</a>\n", i.get_id());
	fprintf(of, "<li> <a href=\"qinc.html?id=%u&direct=1&unused=1&includes=1&n=Unused+Directly+Included+Files\">Unused directly included files</a>\n", i.get_id());
	fprintf(of, "<li> <a href=\"qinc.html?id=%u&n=Files+Including+the+File\">Files including this file</a>\n", i.get_id());
	fprintf(of, "</ul>\n");

	// Metrics
	fprintf(of, "<h2>Metrics</h2>\n<table class='metrics'>\n<tr>"
	    "<th>Metric</th>"
	    "<th>Pre-cpp Value</th>"
	    "<th>Post-cpp Value</th>"
	    "</tr>\n");
	for (int j = 0; j < FileMetrics::metric_max; j++) {
		if (!Metrics::is_file<FileMetrics>(j))
			continue;
		if (Metrics::is_internal<FunMetrics>(j))
			continue;
		fprintf(of, "<tr><td>%s</td>",
		    Metrics::get_name<FileMetrics>(j).c_str());
		if (Metrics::is_pre_cpp<FileMetrics>(j))
			fprintf(of, "<td align='right'>%g</td>",
			    Filedetails::get_pre_cpp_metrics(i).get_metric(j));
		else
			fprintf(of, "<td align='right'>&mdash;</td>");
		if (Metrics::is_post_cpp<FileMetrics>(j))
			fprintf(of, "<td align='right'>%g</td></tr>",
			    Filedetails::get_post_cpp_metrics(i).get_metric(j));
		else
			fprintf(of, "<td align='right'>&mdash;</td></tr>");
	}
	fprintf(of, "</table>\n");

	html_tail(of);
	return 0;
}

static int
source_page(FILE *of, void *)
{
	int id;
	if (!swill_getargs("i(id)", &id)) {
		fprintf(of, "Missing value");
		return 0;
	}
	Fileid i(id);
	const string &pathname = i.get_path();
	html_head(of, "src", string("Source: ") + html(pathname));
	file_hypertext(of, i, false);
	html_tail(of);
	return 0;
}

static int
fedit_page(FILE *of, void *)
{
	if (modification_state == ms_subst) {
		change_prohibited(of);
		return 0;
	}
	prohibit_browsers(of);
	prohibit_remote_access(of);

	int id;
	if (!swill_getargs("i(id)", &id)) {
		fprintf(of, "Missing value");
		return 0;
	}
	Fileid i(id);
	Filedetails::set_hand_edited(i);
	char *re = swill_getvar("re");
	char buff[4096];
	snprintf(buff, sizeof(buff), Option::start_editor_cmd ->get().c_str(), (re ? re : "^"), i.get_path().c_str());
	cerr << "Running " << buff << endl;
	if (system(buff) != 0) {
		html_error(of, string("Launching ") + buff + " failed");
		return 0;
	}
	html_head(of, "fedit", "External Editor");
	fprintf(of, "The editor should have started in a separate window");
	modification_state = ms_hand_edit;
	html_tail(of);
	return 0;
}

static int
query_source_page(FILE *of, void *)
{
	int id;
	if (!swill_getargs("i(id)", &id)) {
		fprintf(of, "Missing value");
		return 0;
	}
	Fileid i(id);
	const string &pathname = i.get_path();
	char *qname = swill_getvar("n");
	if (qname && *qname)
		html_head(of, "qsrc", string(qname) + ": " + html(pathname));
	else
		html_head(of, "qsrc", string("Source with queried elements marked: ") + html(pathname));
	fputs("<p>Use the tab key to move to each marked element.</p>", of);
	file_hypertext(of, i, true);
	html_tail(of);
	return 0;
}

static int
query_include_page(FILE *of, void *)
{
	int id;
	if (!swill_getargs("i(id)", &id)) {
		fprintf(of, "Missing value");
		return 0;
	}
	Fileid f(id);
	const string &pathname = f.get_path();
	char *qname = swill_getvar("n");
	if (qname && *qname)
		html_head(of, "qinc", string(qname) + ": " + html(pathname));
	else
		html_head(of, "qinc", string("Include File Query: ") + html(pathname));
	bool writable = !!swill_getvar("writable");
	bool direct = !!swill_getvar("direct");
	bool unused = !!swill_getvar("unused");
	bool used = !!swill_getvar("used");
	bool includes = !!swill_getvar("includes");
	const FileIncMap &m = includes ? Filedetails::get_includes(f) : Filedetails::get_includers(f);
	html_file_begin(of);
	html_file_set_begin(of);
	for (FileIncMap::const_iterator i = m.begin(); i != m.end(); i++) {
		Fileid f2 = (*i).first;
		const IncDetails &id = (*i).second;
		if ((!writable || !f2.get_readonly()) &&
		    (!direct || id.is_directly_included()) &&
		    (!used || id.is_required()) &&
		    (!unused || !id.is_required())) {
			html_file(of, f2);
			if (id.is_directly_included()) {
				fprintf(of, "<td>line ");
				const set <int> &lines = id.include_line_numbers();
				for (set <int>::const_iterator j = lines.begin(); j != lines.end(); j++)
					fprintf(of, " <a href=\"src.html?id=%u#%d\">%d</a> ", (includes ? f : f2).get_id(), *j, *j);
				if (!id.is_required())
					fprintf(of, " (not required)");
				fprintf(of, "</td>");
			}
			html_file_record_end(of);
		}
	}
	html_file_end(of);
	fputs("</ul>\n", of);
	html_tail(of);
	return 0;
}

static int
logo_page(FILE *fo, void *)
{
	Logo::logo(fo);
	return 0;
}

static int
replacements_page(FILE *of, void *)
{
	prohibit_remote_access(of);
	html_head(of, "replacements", "Identifier Replacements");
	
	if (!opts.is_quiet())
	    cerr << "Creating identifier list" << endl;
	fputs("<p><form action=\"xreplacements.html\" method=\"get\">\n"
		"<table><tr><th>Identifier</th><th>Replacement</th><th>Active</th></tr>\n"
	, of);

	for (IdProp::iterator i = ids.begin(); i != ids.end(); i++) {
		progress(i, ids);
		if (i->second.get_replaced()) {
			fputs("<tr><td>", of);
			html(of, *i);
			fprintf(of,
				"</td><td><input type=\"text\" name=\"r%p\" value=\"%s\" size=\"10\" maxlength=\"256\"></td>"
				"<td><input type=\"checkbox\" name=\"a%p\" value=\"1\" %s></td></tr>\n",
				(void *)&(i->second), i->second.get_newid().c_str(),
				(void *)&(i->second), i->second.get_active() ? "checked" : "");
		}
	}
	if (!opts.is_quiet())
	    cerr << endl;
	fputs("</table><p><INPUT TYPE=\"submit\" name=\"repl\" value=\"OK\">\n", of);
	html_tail(of);
	return 0;
}

// Process an identifier replacements form
static int
xreplacements_page(FILE *of,  void *p)
{
	prohibit_browsers(of);
	prohibit_remote_access(of);

	if (!opts.is_quiet())
	    cerr << "Creating identifier list" << endl;

	for (IdProp::iterator i = ids.begin(); i != ids.end(); i++) {
		progress(i, ids);
		if (i->second.get_replaced()) {
			char varname[128];
			snprintf(varname, sizeof(varname), "r%p", (void *)&(i->second));
			char *subst;
			if ((subst = swill_getvar(varname))) {
				string ssubst(subst);
				i->second.set_newid(ssubst);
			}

			snprintf(varname, sizeof(varname), "a%p", (void *)&(i->second));
			i->second.set_active(!!swill_getvar(varname));
		}
	}
	if (!opts.is_quiet())
	    cerr << endl;
	index_page(of, p);
	return 0;
}


static int
funargrefs_page(FILE *of, void *)
{
	prohibit_remote_access(of);
	html_head(of, "funargrefs", "Function Argument Refactorings");
	fputs("<p><form action=\"xfunargrefs.html\" method=\"get\">\n"
		"<table><tr><th>Function</th><th>Arguments</th><th>Active</th></tr>\n"
	, of);

	for (RefFunCall::store_type::iterator i = RefFunCall::store.begin(); i != RefFunCall::store.end(); i++) {
		fputs("<tr><td>", of);
		html(of, *(i->second.get_function()));
		fprintf(of,
			"</td><td><input type=\"text\" name=\"r%p\" value=\"%s\" size=\"10\" maxlength=\"256\"></td>"
			"<td><input type=\"checkbox\" name=\"a%p\" value=\"1\" %s></td></tr>\n",
			(void *)i->first, i->second.get_replacement().c_str(),
			(void *)i->first, i->second.is_active() ? "checked" : "");
	}
	fputs("</table><p><INPUT TYPE=\"submit\" name=\"repl\" value=\"OK\">\n", of);
	html_tail(of);
	return 0;
}

// Process a function argument refactorings form
static int
xfunargrefs_page(FILE *of,  void *p)
{
	prohibit_browsers(of);
	prohibit_remote_access(of);

	for (RefFunCall::store_type::iterator i = RefFunCall::store.begin(); i != RefFunCall::store.end(); i++) {
		char varname[128];
		snprintf(varname, sizeof(varname), "r%p", (void *)i->first);
		char *subst;
		if ((subst = swill_getvar(varname))) {
			string ssubst(subst);
			i->second.set_replacement(ssubst);
		}

		snprintf(varname, sizeof(varname), "a%p", (void *)i->first);
		i->second.set_active(!!swill_getvar(varname));
	}
	index_page(of, p);
	return 0;
}


static int
write_quit_page(FILE *of, void *exit)
{
	prohibit_browsers(of);
	prohibit_remote_access(of);

	if (exit)
		html_head(of, "quit", "CScout exiting");
	else {
		if (Option::sfile_re_string->get().length() == 0) {
			html_head(of, "save", "Not Allowed");
			fputs("This in-place save and continue operation is not allowed, "
			"because it may corrupt CScout's idea of the source code.  "
			"Either set the filename substitution rule option, "
			"or select the save and exit operation.", of);
			html_tail(of);
			return 0;
		}
		html_head(of, "save", "Saving changes");
	}

	// Determine files we need to process
	IFSet process;
	if (!opts.is_quiet())
	    cerr << "Examining identifiers for renaming" << endl;
	for (IdProp::iterator i = ids.begin(); i != ids.end(); i++) {
		progress(i, ids);
		if (i->second.get_replaced() && i->second.get_active()) {
			Eclass *e = (*i).first;
			IFSet ifiles = e->sorted_files();
			process.insert(ifiles.begin(), ifiles.end());
		}
	}
	if (!opts.is_quiet())
	    cerr << endl;

	// Check for identifier clashes
	Token::found_clashes = false;
	if (Option::refactor_check_clashes->get() && process.size()) {
		if (!opts.is_quiet())
		    cerr << "Checking rename refactorings for name clashes." << endl;
		Token::check_clashes = true;
		// Reparse everything
		Fchar::set_input(engine.get_input_file_path());
		Error::set_parsing(true);
		Pdtoken t;
		do
			t.getnext();
		while (t.get_code() != EOF);
		Error::set_parsing(false);
		Token::check_clashes = false;
	}
	if (Token::found_clashes) {
		fprintf(of, "Renamed identifier clashes detected. Errors reported on console output. No files were saved.");
		html_tail(of);
		return 0;
	}

	if (!opts.is_quiet()) 
	    cerr << "Examining function calls for refactoring" << endl;
	for (RefFunCall::store_type::iterator i = RefFunCall::store.begin(); i != RefFunCall::store.end(); i++) {
		progress(i, RefFunCall::store);
		if (!i->second.is_active())
			continue;
		Eclass *e = i->first;
		IFSet ifiles = e->sorted_files();
		process.insert(ifiles.begin(), ifiles.end());
	}
	if (!opts.is_quiet())
	    cerr << endl;

	// Now do the replacements
	if (!opts.is_quiet())
	    cerr << "Processing files" << endl;
    for (IFSet::const_iterator i = process.begin(); i != process.end(); i++) {
        string err;
        if (!engine.file_refactor(*i, err))
            html_perror(of, err);
    }
	fprintf(of, "A total of %d replacements and %d function call refactorings were made in %d files.",
		engine.get_num_id_replacements(), engine.get_num_fun_call_refactorings(), (unsigned)(process.size()));
	if (exit) {
		fprintf(of, "<p>Bye...</body></html>");
		must_exit = true;
	} else
		html_tail(of);
	return 0;
}

static int
quit_page(FILE *of, void *)
{
	prohibit_browsers(of);
	prohibit_remote_access(of);

	html_head(of, "quit", "CScout exiting");
	fprintf(of, "No changes were saved.");
	fprintf(of, "<p>Bye...</body></html>");
	must_exit = true;
	return 0;
}

// Parse the access control list acl.
static void
parse_acl()
{

	ifstream in;
	string ad, host;
	string fname;

	if (cscout_input_file("acl", in, fname)) {
		cerr << "Parsing ACL from " << fname << endl;
		for (;;) {
			in >> ad;
			if (in.eof())
				break;
			in >> host;
			if (ad == "A") {
				cerr << "Allow from IP address " << host << endl;
				swill_allow(host.c_str());
			} else if (ad == "D") {
				cerr << "Deny from IP address " << host << endl;
				swill_deny(host.c_str());
			} else
				cerr << "Bad ACL specification " << ad << ' ' << host << endl;
		}
		in.close();
	} else {
		cerr << "No ACL found.  Only localhost access will be allowed." << endl;
		swill_allow("127.0.0.1");
	}
}

// Included file site information
// See warning_report
class SiteInfo {
private:
	bool required;		// True if this site contains at least one required include file
	set <Fileid> files;	// Files included here
public:
	SiteInfo(bool r, Fileid f) : required(r) {
		files.insert(f);
	}
	void update(bool r, Fileid f) {
		required |= r;
		files.insert(f);
	}
	const set <Fileid> & get_files() const { return files; }
	bool is_required() const { return required; }
};

// Generate a warning report
static void
warning_report()
{
	struct {
		const char *message;
		const char *query;
	} reports[] = {
		{ "unused project scoped writable identifier",
		  "L:writable:unused:pscope" },
		{ "unused file scoped writable identifier",
		  "L:writable:unused:fscope" },
		{ "unused writable macro",
		  "L:writable:unused:macro" },
		{ "writable identifier should be made static",
		  "T:writable:obj:pscope" }, // xfile is implicitly 0
	};

	// Generate identifier warnings
	for (unsigned i = 0; i < sizeof(reports) / sizeof(reports[0]); i++) {
		IdQuery query(reports[i].query);

		csassert(query.is_valid());
		for (IdProp::iterator j = ids.begin(); j != ids.end(); j++) {
			if (!query.eval(*j))
				continue;
			const Tokid t = *((*j).first->get_members().begin());
			const string &id = (*j).second.get_id();
			cerr << t.get_path() << ':' <<
				Filedetails::get_line_number(t.get_fileid(), t.get_streampos()) << ": " <<
				id << ": " << reports[i].message << endl;
		}
	}

	/*
	 * Generate unneeded include file warnings
	 * The hard work has already been done by Fdep::mark_required()
	 * Here we do some additional processing, because
	 * a given include directive can include different files on different
	 * compilations (through different include paths or macros)
	 * Therefore maintain a map for include directive site information:
	 */

	typedef map <int, SiteInfo> Sites;
	Sites include_sites;

	for (const auto &file : engine.get_files()) {
		if (file.get_readonly() ||		// Don't report on RO files
		    !Filedetails::is_compilation_unit(file) ||		// Algorithm only works for CUs
		    file == engine.get_input_file_id() ||		// Don't report on main file
		    Filedetails::get_includers(file).size() > 1)	// For files that are both CUs and included
							// by others all bets are off
			continue;
		const FileIncMap &m = Filedetails::get_includes(file);
		// Find the status of our include sites
		include_sites.clear();
		for (FileIncMap::const_iterator j = m.begin(); j != m.end(); j++) {
			Fileid f2 = (*j).first;
			const IncDetails &id = (*j).second;
			if (!id.is_directly_included())
				continue;
			const set <int> &lines = id.include_line_numbers();
			for (set <int>::const_iterator k = lines.begin(); k != lines.end(); k++) {
				Sites::iterator si = include_sites.find(*k);
				if (si == include_sites.end())
					include_sites.insert(Sites::value_type(*k, SiteInfo(id.is_required(), f2)));
				else
					(*si).second.update(id.is_required(), f2);
			}
		}
		// And report those containing unused files
		Sites::const_iterator si;
		for (si = include_sites.begin(); si != include_sites.end(); si++)
			if (!(*si).second.is_required()) {
				const set <Fileid> &sf = (*si).second.get_files();
				int line = (*si).first;
				for (set <Fileid>::const_iterator fi = sf.begin(); fi != sf.end(); fi++)
					cerr << file.get_path() << ':' <<
						line << ": " <<
						"(" << Filedetails::get_pre_cpp_const_metrics(file).get_int_metric(Metrics::em_nuline) << " unprocessed lines)"
						" unused included file " <<
						fi->get_path() <<
						endl;
			}
	}
}



/*
 * Read files with tokens classes and identifier attributes
 * and merge them together
 */
static void
merge_tokens(char **argv)
{
	// Skip over cscout -M
	argv += 2;

	// Files in the order they appear in argv
	enum arg_files {
		in_eclasses_attached,
		in_eclasses_original,
		in_ids,
		in_functionids_attached,
		in_functionids_original,
		in_idproj,
		new_eclasses,
		new_ids,
		new_functionids,
		new_idproj,
		new_functionid_to_global_map,
	};

	/*
	 * Example invocation:
	 * cscout -M \
	 *   eclasses-a-5.txt \
	 *   eclasses-o-5.txt \
	 *   ids-5.txt \
	 *   functionids-a-5.txt \
	 *   functionids-o-5.txt \
	 *   idproj-5.txt \
	 *   new-eclasses-5.csv \
	 *   new-ids-5.csv \
	 *   new-functionds-5.csv
	 *   new-idproj-5.csv
	 *   new-functionid-to-global-map.csv
	 */
	Dbtoken::add_eclasses_attached(argv[in_eclasses_attached]);
	Dbtoken::process_eclasses_original(argv[in_eclasses_original]);
	Dbtoken::read_write_functionids(
	    argv[in_functionids_attached],
	    argv[in_functionids_original],
	    argv[new_functionids],
	    argv[new_functionid_to_global_map]);
	Dbtoken::write_eclasses(argv[new_eclasses]);
	Dbtoken::read_ids(argv[in_ids]);
	Dbtoken::write_ids(argv[in_ids], argv[new_ids]);
	Dbtoken::read_write_idproj(argv[in_idproj], argv[new_idproj]);

	exit(0);
}

int
main(int argc, char *argv[])
{
	Pdtoken t;
	Debug::db_read();

	opts.parse_args(argc, argv);

	if (opts.do_merge)
		merge_tokens(argv);

	if (!opts.log_file.empty()) {
		FILE *logfile;
		if ((logfile = fopen(opts.log_file.c_str(), "a")) == NULL) {
			perror(opts.log_file.c_str());
			exit(1);
		}
		swill_log(logfile);
	}

	// We require exactly one argument
	if (argv[optind] == NULL || argv[optind + 1] != NULL)
		usage(argv[0]);

	if (opts.is_web_server_mode()) {
		if (!swill_init(opts.portno)) {
			cerr << "Couldn't initialize our web server on port " << opts.portno << endl;
			exit(1);
		}

		Option::initialize();
		options_load();
		parse_acl();
	}

	if (opts.process_mode == CscoutOptions::pm_database) {
		if (!Sql::setEngine(opts.db_engine))
			return 1;
		cout << Sql::getInterface()->begin_commands();
		workdb_schema(Sql::getInterface(), cout);
	}

	Project::set_current_project("unspecified");

	// Set the contents of the master file as immutable
	Fileid fi = Fileid(argv[optind]);
	fi.set_readonly(true);

	// Pass 1: process master file loop
	Fchar::set_input(argv[optind]);
	Error::set_parsing(true);
	do
		t.getnext();
	while (t.get_code() != EOF);
	Error::set_parsing(false);

	if (opts.process_mode == CscoutOptions::pm_preprocess)
		return 0;

	engine.set_input_file_id(Fileid(argv[optind]));

	Filedetails::unify_identical_files();

	if (opts.process_mode == CscoutOptions::pm_obfuscation)
		return obfuscate();

	// Pass 2: Create web pages
	engine.collect_active_files();



	if (opts.process_mode != CscoutOptions::pm_compile) {
		swill_handle("sproject.html", select_project_page, 0);
		swill_handle("replacements.html", replacements_page, 0);
		swill_handle("xreplacements.html", xreplacements_page, NULL);
		swill_handle("funargrefs.html", funargrefs_page, 0);
		swill_handle("xfunargrefs.html", xfunargrefs_page, NULL);
		swill_handle("options.html", options_page, 0);
		swill_handle("soptions.html", set_options_page, 0);
		swill_handle("save_options.html", save_options_page, 0);
		swill_handle("sexit.html", write_quit_page, "exit");
		swill_handle("save.html", write_quit_page, 0);
		swill_handle("qexit.html", quit_page, 0);
	}

	/*
	 * Populate the EC identifier member and the directory tree.
	 * Set several file and function metrics.
	 */
	Call::populate_macro_map();
	for (const auto &file : engine.get_files()) {
		engine.file_analyze(file);
		dir_add_file(file);
	}

	// Update file and function metrics
	file_msum.summarize_files();
	fun_msum.summarize_functions();

	// Set runtime file dependencies
	GlobObj::set_file_dependencies();

	// Set xfile and  metrics for each identifier
	if (!opts.is_quiet())
	    cerr << "Processing identifiers" << endl;
	for (IdProp::iterator i = ids.begin(); i != ids.end(); i++) {
		progress(i, ids);
		Eclass *e = (*i).first;
		IFSet ifiles = e->sorted_files();
		(*i).second.set_xfile(ifiles.size() > 1);
		// Update metrics
		id_msum.add_unique_id(e);
	}
	if (!opts.is_quiet())
	    cerr << endl;

	if (DP())
		cout << "Size " << file_msum.get_pre_cpp_total(Metrics::em_nchar) << endl;

	if (opts.process_mode == CscoutOptions::pm_database) {
		workdb_rest(Sql::getInterface(), cout);
		Call::dumpSql(Sql::getInterface(), cout);
		cout << Sql::getInterface()->end_commands();
#ifdef LINUX_STAT_MONITOR
		char buff[100];
		snprintf(buff, sizeof(buff), "cat /proc/%u/stat >%u.stat", getpid(), getpid());
		if (system(buff) != 0) {
			fprintf(stderr, "Unable to run %s\n", buff);
			exit(1);
		}
#endif
		return 0;
	}

	if (opts.process_mode != CscoutOptions::pm_compile) {
		swill_handle("src.html", source_page, NULL);
		swill_handle("qsrc.html", query_source_page, NULL);
		swill_handle("fedit.html", fedit_page, NULL);
		swill_handle("file.html", file_page, NULL);
		swill_handle("dir.html", dir_page, NULL);

		// Identifier query and execution
		swill_handle("iquery.html", iquery_page, NULL);
		swill_handle("xiquery.html", xiquery_page, NULL);
		// File query and execution
		swill_handle("filequery.html", filequery_page, NULL);
		swill_handle("xfilequery.html", xfilequery_page, NULL);
		swill_handle("qinc.html", query_include_page, NULL);

		// Function query and execution
		swill_handle("funquery.html", funquery_page, NULL);
		swill_handle("xfunquery.html", xfunquery_page, NULL);

		swill_handle("id.html", identifier_page, NULL);
		swill_handle("fun.html", function_page, NULL);
		swill_handle("funlist.html", funlist_page, NULL);
		swill_handle("funmetrics.html", function_metrics_page, NULL);
		swill_handle("filemetrics.html", file_metrics_page, NULL);
		swill_handle("idmetrics.html", id_metrics_page, NULL);

		graph_handle("cgraph", cgraph_page);
		graph_handle("fgraph", fgraph_page);
		graph_handle("cpath", cpath_page);

		swill_handle("about.html", about_page, NULL);
		swill_handle("setproj.html", set_project_page, NULL);
		swill_handle("logo.png", logo_page, NULL);
		swill_handle("index.html", index_page, 0);
	}


	if (file_msum.get_pre_cpp_writable(Metrics::em_nuline)) {
		ostringstream msg;
		msg << file_msum.get_pre_cpp_writable(Metrics::em_nuline) <<
		    " conditionally compiled writable lines" << endl <<
		    "(out of a total of " <<
		    (int)file_msum.get_pre_cpp_writable(Metrics::em_nline) <<
		    " writable lines) were not processed";
		Error::error(E_WARN, msg.str(), false);
	}

	CTag::save();
	if (opts.process_mode == CscoutOptions::pm_report) {
		if (!must_exit)
			warning_report();
		return (0);
	}

#ifdef PICO_QL
	if (opts.pico_ql) {
		pico_ql_register(&files, "files");
		pico_ql_register(&Identifier::ids, "ids");
		pico_ql_register(&Tokid::tm, "tm");
		pico_ql_register(&Call::functions(), "fun_map");
		while (pico_ql_serve(opts.portno))
			;
		return (0);
	}
#endif

	if (opts.process_mode == CscoutOptions::pm_call_graph) {
		cerr << "Producing call graphs for: ";
		for (string d : opts.call_graphs) cerr << d << " ";
		cerr << endl;
		produce_call_graphs(opts.call_graphs);

		return (0);
	}

	if (DP())
		cout  << "Tokid EC map size is " << Tokid::map_size() << endl;
	if (opts.process_mode == CscoutOptions::pm_compile)
		return (0);
	// Serve web pages
	if (!must_exit)
		cerr << "CScout is now ready to serve you at http://localhost:" << opts.portno << endl;
	if (browse_only)
		swill_setfork();
	while (!must_exit)
		swill_serve();

#ifdef NODE_USE_PROFILE
	cout << "Type node count = " << Type_node::get_count() << endl;
#endif
	return (0);
}
