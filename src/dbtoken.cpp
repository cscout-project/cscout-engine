/*
 * (C) Copyright 2024-2026 Diomidis Spinellis
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
 * A token constructed from a database dump.
 *
 */

#include <iostream>
#include <fstream>
#include <map>
#include <string>
#include <deque>
#include <sstream>
#include <tuple>
#include <cstdint>
#include <compare>
#include <utility>

using namespace std;

#include "fileid.h"
#include "tokid.h"
#include "token.h"
#include "dbtoken.h"
#include "eclass.h"
#include "fileid.h"
#include "attr.h"
#include "cpp.h"
#include "debug.h"

template <typename StreamType>
static void
verify_open(const char *path, StreamType &f)
{
	if (!f.is_open()) {
		cerr << "Error opening " << path << '\n';
		exit(1);
	}
}

static int line_number;

static int n_errors = 0;

static void
issue_error(const char *file_name, const string &context, Tokid ti)
{
	n_errors += 1;
	cerr << file_name << '(' << line_number << "): " << context
		    << ": missing EC for file "
		    << ti.get_fileid().get_id() << " offset "
		    << (unsigned)ti.get_streampos() << "\n";
}

static void
issue_error(const char *file_name, const string &s)
{
	n_errors += 1;
	cerr << file_name << '(' << line_number << "): malformed input: "
		<< s << "\n";
}

static void
exit_on_error()
{
	if (n_errors > 0) {
		cerr << "Issued " << n_errors << " errors; exiting.\n";
		exit(1);
	}
}

/*
 * Return the tokid with its negative fid
 * ECs are supposed to be unique for each tokid.
 * When the second file is read some of its ECs may exactly mirror
 * existing ECs, but others may have the same identifying tokid but
 * a different length.  To keep the two EC sets distinct, we keep the
 * second twin set of ECs with their file identifier stored as its negative
 * value.
 * Then, when writing out the ECs, we skip twin ECs that are exact mirrors
 * and write out the others in their non-negative form.
 */
static Tokid
twin(const Tokid &ti)
{
	return Tokid(Fileid(-ti.get_fileid().get_id()), ti.get_streampos());
	//                  ^
}

// Convert a Tokid into its canonical form by removing the twin marker.
static Tokid
untwin(const Tokid &ti)
{
	if (ti.get_fileid().get_id() < 0)
		return twin(ti);
	return ti;
}

// Convert a dequeTpart into its canonical form by untwinning any twins.
static dequeTpart
untwin(const dequeTpart &parts)
{
	dequeTpart r;
	for (auto i : parts)
		r.push_back(Tpart(untwin(i.get_tokid()), i.get_len()));
	return (r);
}


// Return the Eclass for the specified tokid or its twin.
// Return the shortest of the two, which may be the result of subsequent
// splitting.
// If an EC isn't found return NULL.
static Eclass *
check_ec(const Tokid &ti)
{
	Eclass *ec = ti.check_ec();
	Eclass *ec_twin = twin(ti).check_ec();

	if (ec == NULL)
		return ec_twin;
	else if (ec_twin == NULL)
		return ec;
	else if (ec->get_len() <= ec_twin->get_len())
		return ec;
	return ec_twin;
}

// Store of maximum recorded offset per file
static vector <unsigned long> max_fid_offset;

// Record an encountered maximum offset for the given file id.
static void
record_offset(size_t fid, unsigned long offset)
{
	if (fid >= max_fid_offset.size())
		max_fid_offset.resize(fid + 1);
	max_fid_offset[fid] = max(offset, max_fid_offset[fid]);
}

/*
 * First, read tokids and their equivalence classes from the attached
 * (often larger) file.
 * As ECs come from a single file, all are guaranteed to be unique (for a
 * given fileid, tokid) and non-overlapping. Therefore, create (non-twin)
 * ECs or add entries to existing ECs.
 */
static void
add_eclasses_attached(const char *in_path)
{
	ifstream input(in_path);
	verify_open(in_path, input);

	Eclass *ec = NULL; // EC pointer as created / found
	intptr_t ecid, prev_ecid = 0; // EC identifier read from file

	string line_record;
	line_number = 0;
	while (getline(input, line_record)) {
		line_number++;

		istringstream line_stream(line_record);
		int fid;
		unsigned long offset;
		int len;
		if (!(line_stream >> fid >> offset >> len >> ecid)) {
			issue_error(in_path, line_record);
			continue;
		}

		record_offset(fid, offset);
		Tokid ti(Fileid(fid), offset);

		if (DP())
			cout << in_path << '(' << line_number << ") ti " << ti << '\n';

		// Add new ECs for not encountered ones
		if (ecid == prev_ecid) {
			// Same EC as previous entry
			ec->add_tokid(ti);
			if (DP())
				cout << "Add attached ti " << twin(ti) << '\n';
			continue;
		}

		ec = new Eclass(ti, len);
		if (DP())
			cout << "Create attached ti " << twin(ti) << '\n';
		prev_ecid = ecid;
	}
	exit_on_error();
}

/*
 * Second, read tokids and their equivalence classes from the original
 * (sometimes empty) file.
 * Create ECs or add entries to existing ECs using as an identifier the
 * twin tokid to keep them distinct from the attached ones.
 */
static void
add_eclasses_original(const char *in_path)
{
	ifstream input(in_path);
	verify_open(in_path, input);

	Eclass *ec = NULL; // EC pointer as created / found
	intptr_t ecid, prev_ecid = 0; // EC identifier read from file

	string line_record;
	line_number = 0;
	while (getline(input, line_record)) {
		line_number++;

		istringstream line_stream(line_record);
		int fid;
		unsigned long offset;
		int len;
		if (!(line_stream >> fid >> offset >> len >> ecid)) {
			issue_error(in_path, line_record);
			continue;
		}

		record_offset(fid, offset);
		Tokid ti(Fileid(fid), offset);

		if (DP())
			cout << in_path << '(' << line_number << ") ti " << ti << '\n';

		if (ecid == prev_ecid) {
			ec->add_tokid(twin(ti));
			if (DP())
				cout << "Add original ti " << twin(ti) << '\n';
			continue;
		}

		// Add new ECs for not encountered ones
		ec = ti.check_ec();
		if (ec == nullptr || ec->get_len() != len) {
			// Same EC as previous entry
			// fids from original are stored -ve to avoid clashes
			ec = new Eclass(twin(ti), len);
			if (DP())
				cout << "Create original ti " << twin(ti) << '\n';
		}
		prev_ecid = ecid;
	}
	exit_on_error();
}

// An abstraction for data regarding ECs to merge
class MergeGatherer {
private:
	Tokid ti;	// Current file and position
	Eclass *ec;	// Identified EC
	Dbtoken token;	// Gathered token to merge
public:
	MergeGatherer(Eclass *ec, const Tokid &ti) : ti(ti), ec(ec) {}

	// Forwarding methods
	int get_len() const { csassert(ec); return ec->get_len(); }
	void add_part(Tokid ti, int len) { token.add_part(ti, len); }
	Eclass *check_ec() const { return ti.check_ec(); }
	const Dbtoken &get_token() const { return token; }
	Tokid get_tokid() const { return ti; }
	auto get_fileid() const { return ti.get_fileid(); }
	cs_offset_t get_streampos() const { return ti.get_streampos(); }
	void set_ec(Eclass *ec) { this->ec = ec; }
	void set_tokid(const Tokid &ti) { this->ti = ti; }
	auto get_parts_size() const { return token.get_parts_size(); }

	// Advance file position to the end of the current EC, adding the token.
	void advance() {
		int len = get_len();
		add_part(ti, len);
		ti += len;
	}

	// From current position
	void set_ec() { ec = check_ec(); }

	// Return the end position.
	cs_offset_t get_end_pos() const {
		return get_streampos() + get_len();
	}

	// Compare end positions
	auto operator<=>(const MergeGatherer &other) const {
		return get_end_pos() <=> other.get_end_pos();
	}

	friend std::ostream &
	operator<<(std::ostream &os, const MergeGatherer &g) {
		os << "MergeGatherer {\n" << g.ti
		<< "\nEC: {" << g.ec << ' ';
		if (g.ec)
			os << *g.ec;
		return os << "}\nAssembled token: " << g.token
		<< '}';
	}
};


/*
 * Unify a chain of possible overlapping ECs starting by the leader
 * and continuing by the twin follower until both reach their end.
 * For examples, see csmerge test files tokens-split-{12,13}.rdbu.
 */
void
unify_chain_ecs(MergeGatherer leader)
{

	MergeGatherer follower(nullptr, twin(leader.get_tokid()));

	// For the whole chain
	for (;;) {
		if (DP())
			cout << "unify_chain_ecs loop\nLeader: " << leader
				<< "\nFollower: " << follower << '\n';
		// Invariants
		csassert(leader.check_ec() != nullptr);
		csassert(follower.get_streampos() >= leader.get_streampos());

		// Try to find a follower EC overlapping with the leader.
		Eclass *ec = nullptr;
		int leader_offset = max(leader.get_streampos(),
		    follower.get_streampos()) - leader.get_streampos();
		int leader_offset_start = leader_offset;
		Tokid ti(follower.get_fileid(),
		    leader.get_streampos() + leader_offset);
		if (DP())
			cout << "Before scan leader_offset: " << leader_offset
				<< " Follower Tokid: " << ti << '\n';
		auto leader_len = leader.get_len();
		for (; leader_offset < leader_len; leader_offset++) {
			if ((ec = ti.check_ec()) != nullptr)
				break;
			ti++;
		}

		if (DP()) {
			cout << "After scan leader offset: " << leader_offset
				<< "\nFollower EC: ";
			if (ec) cout << *ec; else cout << "null\n";
			cout << "New follower tokid: " << ti << '\n';
		}
		int follower_gap = leader_offset - leader_offset_start;
		if (follower_gap) {
			// Follower missing or starts within leader but
			// after a gap.  Pad it with missing EC.
			follower.set_ec(new Eclass(follower.get_tokid(),
				    follower_gap));
			follower.advance();	// ... to gap start.
			if (DP())
				cout << "Pad follower to: " << follower << "\n";
		}
		if (ec == nullptr) {
			if (DP())
				cout << "No follower found\n";
			// End of chain; we're done.
			leader.advance();
			if (follower.get_parts_size())
				Token::unify(leader.get_token(),
				    follower.get_token());
			return;
		}

		follower.set_tokid(ti);
		follower.set_ec(ec);
		if (DP())
			cout << "Set follower EC: " << follower << '\n';

		// We now have a follower.  Assess the two endings.
		auto cmp = (leader <=> follower);
		if (cmp < 0) {
			if (DP())
				cout << "Leader ends before follower; swap.\n";
			leader.advance();
			swap(leader, follower);
			leader.set_ec();
		} else if (cmp > 0) {
			if (DP())
				cout << "Leader ends after follower.\n";
			follower.advance();
		} else {
			// Both end at the same place; cash-in progress.
			if (DP())
				cout << "Leader ends with follower.\n"
					<< "Leader: " << leader
					<< "\nFollower: " << follower << '\n';
			leader.advance();
			follower.advance();
			Token::unify(leader.get_token(), follower.get_token());
			return;
		}
	}
}

// Unify the ECs of the specified file.
static void
unify_file_ecs(int fid, unsigned long max_offset)
{
	Tokid cursor(Fileid(fid), 0);
	Tokid end(Fileid(fid), max_offset);

	if (DP() && max_offset > 0)
		cout << "unify_file_ecs to " << end;
	for (cursor = Tokid(Fileid(fid), 0); cursor <= end; cursor++) {
		Eclass *ec;
		if ((ec = cursor.check_ec()) != nullptr)
			unify_chain_ecs(MergeGatherer(ec, cursor));
		else if ((ec = twin(cursor).check_ec()) != nullptr)
			unify_chain_ecs(MergeGatherer(ec, twin(cursor)));
		else
			continue;
	}
}

/*
 * Third, go through all files looking for twins
 * (possibly of different starting positions and length).
 * Put together tokens of the same length, and unify them.
 *
 * Go through the files rather than the EC map, because the map changes
 * as tokens get unified.
 */
static void
merge_loaded_eclasses()
{
	for (size_t i = 0; i < max_fid_offset.size(); ++i)
		unify_file_ecs(i, max_fid_offset[i]);
}

/*
 * Merge tokids and ECs from the original (sometimes empty) file
 * and the attached file.
 * This requires two passes, one on each file.
 */
void
Dbtoken::process_eclasses(const char *original_path,
    const char *attached_path)
{
	Fileid::disable_filedetails();
	Project::set_current_project("csmerge");

	// Order is important here.
	if (DP())
		cout << "Add attached classes\n";
	add_eclasses_attached(attached_path);
	if (DP())
		cout << "Add original classes\n";
	add_eclasses_original(original_path);

	if (DP())
		cout << "Merge loaded ECs\n";
	merge_loaded_eclasses();
}

// Output tokids and their equivalence classes to file named f
void
Dbtoken::write_eclasses(const char *out_path)
{
	ofstream of(out_path);
	verify_open(out_path, of);

	for (auto i = Tokid::begin_ec(); i != Tokid::end_ec(); ++i) {
		Tokid ti(i->first);
		int fid = ti.get_fileid().get_id();

		Eclass *ec = i->second;

		if (fid < 0) {
			// This is a twin (negative) EC; obtain its sibling.
			Eclass *ec_twin = twin(ti).check_ec();
			if (ec_twin)
				// Mirror EC exists; ignore
				continue;
			else
				// No twin EC, output the correct fid
				fid = -fid;
		}

		of
		    << fid << ','
		    << (unsigned)ti.get_streampos() << ','
		    << ptr_offset(ec) << '\n';
	}
}

// Read identifiers from in_path and set the EC attributes
void
Dbtoken::read_ids(const char *in_path)
{
	int dbid;
	int fid;
	unsigned long offset;
	intptr_t ecid;
	string name;
	bool v_readonly;
	bool v_undefined_macro;
	bool v_undefed_macro;
	bool v_redefined_same_macro;
	bool v_redefined_diff_macro;
	bool v_macro;
	bool v_fun_macro;
	bool v_macro_arg;
	bool v_cpp_const;
	bool v_cpp_str_val;
	bool v_def_c_const;
	bool v_def_not_c_const;
	bool v_exp_c_const;
	bool v_exp_not_c_const;
	bool v_used_in_ice;
	bool v_ordinary;
	bool v_suetag;
	bool v_sumember;
	bool v_label;
	bool v_typedef;
	bool v_enumeration;
	bool v_yacc;
	bool v_cfunction;
	bool v_cscope;
	bool v_lscope;
	bool v_unused;

	ifstream input(in_path);
	verify_open(in_path, input);

	line_number = 0;
	while (input >> dbid >> fid >> offset >> ecid >> name
		>> v_readonly
		>> v_undefined_macro
		>> v_undefed_macro
		>> v_redefined_same_macro
		>> v_redefined_diff_macro
		>> v_macro
		>> v_fun_macro
		>> v_macro_arg
		>> v_cpp_const
		>> v_cpp_str_val
		>> v_def_c_const
		>> v_def_not_c_const
		>> v_exp_c_const
		>> v_exp_not_c_const
		>> v_used_in_ice

		>> v_ordinary
		>> v_suetag
		>> v_sumember
		>> v_label
		>> v_typedef
		>> v_enumeration
		>> v_yacc
		>> v_cfunction
		>> v_cscope
		>> v_lscope
		>> v_unused) {

		line_number++;
		Tokid ti(Fileid(fid), offset);
		int name_length = name.length();
		int covered = 0;
		while (covered < name_length) {
			Eclass *ec = check_ec(ti);

			if (ec == NULL) {
				issue_error(in_path, "Obtain id EC", ti);
				goto next_line;
			}

			if (v_readonly) ec->set_attribute(is_readonly);
			if (v_undefined_macro) ec->set_attribute(is_undefined_macro);
			if (v_undefed_macro) ec->set_attribute(is_undefed_macro);
			if (v_redefined_same_macro) ec->set_attribute(is_redefined_same_macro);
			if (v_redefined_diff_macro) ec->set_attribute(is_redefined_diff_macro);
			if (v_macro) ec->set_attribute(is_macro);
			if (v_fun_macro) ec->set_attribute(is_fun_macro);
			if (v_macro_arg) ec->set_attribute(is_macro_arg);
			if (v_cpp_const) ec->set_attribute(is_cpp_const);
			if (v_cpp_str_val) ec->set_attribute(is_cpp_str_val);
			if (v_def_c_const) ec->set_attribute(is_def_c_const);
			if (v_def_not_c_const) ec->set_attribute(is_def_not_c_const);
			if (v_exp_c_const) ec->set_attribute(is_exp_c_const);
			if (v_exp_not_c_const) ec->set_attribute(is_exp_not_c_const);
			if (v_used_in_ice) ec->set_attribute(is_used_in_ice);

			if (v_ordinary) ec->set_attribute(is_ordinary);
			if (v_suetag) ec->set_attribute(is_suetag);
			if (v_sumember) ec->set_attribute(is_sumember);
			if (v_label) ec->set_attribute(is_label);
			if (v_typedef) ec->set_attribute(is_typedef);
			if (v_enumeration) ec->set_attribute(is_enumeration);
			if (v_yacc) ec->set_attribute(is_yacc);
			if (v_cfunction) ec->set_attribute(is_cfunction);
			if (v_cscope) ec->set_attribute(is_cscope);
			if (v_lscope) ec->set_attribute(is_lscope);
			// Unused is a derived attribute, so it is not set

			covered += ec->get_len();
			ti += ec->get_len();
		}
next_line:
		continue;
	}
	exit_on_error();
}


/*
 * Simplified version of is_unused.
 * This can be improved by saving and merging is_declared_unused
 * attribute and the identical files table.
 */
static bool
is_unused_eclass(Eclass *e)
{
	set <Tokid> eclass_tokids;

	for (Tokid ti : e->get_members()) {
		// Untwin, as twins refer to the same file.
		if (ti.get_fileid().get_id() < 0)
			ti = twin(ti);
		if (DP())
			cout << "is_unused EC: " << e << "\nTokid: " << ti;
		eclass_tokids.insert(ti);
		if (eclass_tokids.size() > 1) {
			return false;
		}
	}
	return true;
}

set <Eclass *> Dbtoken::dumped_ids;

// Dump the details of a single identifier
void
Dbtoken::dump_id(ostream &of, Eclass *e, const string &name)
{
	if (!dumped_ids.insert(e).second)
		return;
	of <<
	     ptr_offset(e) << "," <<
	     name << ',' <<
	     e->get_attribute(is_readonly) << ',' <<
	     e->get_attribute(is_undefined_macro) << ',' <<
	     e->get_attribute(is_undefed_macro) << ',' <<
	     e->get_attribute(is_redefined_same_macro) << ',' <<
	     e->get_attribute(is_redefined_diff_macro) << ',' <<
	     e->get_attribute(is_macro) << ',' <<
	     e->get_attribute(is_fun_macro) << ',' <<
	     e->get_attribute(is_macro_arg) << ',' <<
	     e->get_attribute(is_cpp_const) << ',' <<
	     e->get_attribute(is_cpp_str_val) << ',' <<
	     e->get_attribute(is_def_c_const) << ',' <<
	     e->get_attribute(is_def_not_c_const) << ',' <<
	     e->get_attribute(is_exp_c_const) << ',' <<
	     e->get_attribute(is_exp_not_c_const) << ',' <<
	     e->get_attribute(is_used_in_ice) << ',' <<

	     e->get_attribute(is_ordinary) << ',' <<
	     e->get_attribute(is_suetag) << ',' <<
	     e->get_attribute(is_sumember) << ',' <<
	     e->get_attribute(is_label) << ',' <<
	     e->get_attribute(is_typedef) << ',' <<
	     e->get_attribute(is_enumeration) << ',' <<
	     e->get_attribute(is_yacc) << ',' <<
	     e->get_attribute(is_cfunction) << ',' <<
	     e->get_attribute(is_cscope) << ',' <<
	     e->get_attribute(is_lscope) << ',' <<
	     is_unused_eclass(e) <<
	     '\n';
}

// Dump identifiers of file in_path to file out_path
void
Dbtoken::write_ids(const char *in_path, const char *out_path)
{
	// Iterate over the read identifiers, obtaining new EC and new len
	// Dump len part adjust offset and repeat
	ifstream in(in_path);
	verify_open(out_path, in);

	ofstream out(out_path);
	verify_open(out_path, out);

	string line_record;
	line_number = 0;
	while (getline(in, line_record)) {
		line_number++;

		istringstream line_stream(line_record);
		int dbid;
		int fid;
		unsigned long offset;
		intptr_t ecid;
		string name;
		if (!(line_stream >> dbid >> fid >> offset >> ecid >> name)) {
			issue_error(in_path, line_record);
			continue;
		}

		Tokid ti(Fileid(fid), offset);
		int name_length = name.length();
		int covered = 0;
		while (covered < name_length) {
			Eclass *ec = check_ec(ti);

			if (ec == NULL) {
				issue_error(in_path, "Obtain next id EC", ti);
				goto next_line;
			}

			auto len = ec->get_len();
			dump_id(out, ec, name.substr(covered, len));

			covered += len;
			ti += len;
		}
next_line:
		continue;
	}
	dumped_ids.clear();
	exit_on_error();
}


/*
 * Read functionid data and generate merged functionid and
 * corresponding local to global map.
 */
void
Dbtoken::read_write_functionids(
    const char *fid_in_path_attached,
    const char *fid_in_path_original,
    const char *fid_out_path,
    const char *map_out_path)
{
	Dbtoken token;
	// Keep track of new assigned functionids. Map the
	// constituent token parts (as read from functionids) and
	// the function name into the new functionid. The name is
	// needed for macro-generated functions that share a file
	// and offset while expanding to different identifiers.
	map <pair<dequeTpart, string>, int> fnid;

	// New functionid: id, ordinal, eid
	ofstream fid_out(fid_out_path);
	verify_open(fid_out_path, fid_out);

	// functionid_to_global_map: dbid, id, global_map
	ofstream map_out(map_out_path);
	verify_open(map_out_path, map_out);

	// Called for each complete functionid read.
	auto complete_functionid = [&](intptr_t functionid, int dbid, const string& name) {
		if (functionid == -1)
			return;

		int new_id = static_cast<int>(fnid.size()) + 1;

		if (DP())
			cout << "\nLook for token " << token;

		// Obtain constituents from the EC/twin universe
		// in which ECs are stored, but map old to new
		// functionids using the canonical Tokid representation
		// to keep the map key unique and to write out
		// canonical tokids.
		dequeTpart parts(untwin(token.constituents()));
		auto [it, inserted] = fnid.insert({{parts, name}, new_id});
		if (DP())
			cout << "Function " << functionid << " inserted: " << inserted << '\n';
		int global_functionid = it->second;
		// Write the global map record.
		map_out
			<< dbid << ','
			<< functionid << ','
			<< global_functionid
			<< '\n';
		if (!inserted)
			// This functionid has already been written.
			return;

		// Write out the new functionid.
		int ordinal = 0;
		for (auto i : parts) {
			Tokid ti(i.get_tokid());
			Eclass *ec = check_ec(ti);


			// New functionid: id, ordinal, eid
			fid_out
				<< global_functionid << ','
				<< ordinal++ << ','
				<< ti.get_fileid().get_id() << ','
				<< (unsigned)(ti.get_streampos()) << ','
				<< ec->get_len()
				<< '\n';
		}
	};

	// Called to read a functionid input file.
	// The twinning argument specifies whether Tokids should
	// be created in the twin space.
	auto read_functionid = [&](const char *fid_in_path, bool twinning) {
		// functionid: dbid, functionid, fid, foffset, len
		ifstream in(fid_in_path);
		verify_open(fid_out_path, in);

		string line_record;
		line_number = 0;
		intptr_t prev_functionid = -1;
		int prev_dbid = -1;
		string prev_name;


		while (getline(in, line_record)) {
			line_number++;
			int len;
			if (DP())
				cout << fid_in_path << '(' << line_number << "): " << line_record << '\n';

			int dbid;
			intptr_t functionid;
			int fileid;
			unsigned long offset;

			istringstream line_stream(line_record);
			string name;
			if (!(line_stream >> dbid >> functionid >> fileid >> offset >>len)) {
				issue_error(fid_in_path, line_record);
				continue;
			}
			line_stream >> name;

			if (functionid != prev_functionid) {
				complete_functionid(prev_functionid, prev_dbid, prev_name);
				token.clear();
			}
			Tokid ti(Fileid(fileid), offset);
			if (twinning)
				token.add_part(twin(ti), len);
			else
				token.add_part(ti, len);

			if (DP())
				cout << "fileid: " << fileid
				    << " offset: " << offset
				    << " len: " << len
				    << '\n';

			prev_functionid = functionid;
			prev_dbid = dbid;
			prev_name = name;
		}
		complete_functionid(prev_functionid, prev_dbid, prev_name);
	};

	read_functionid(fid_in_path_attached, false);
	read_functionid(fid_in_path_original, true);
	exit_on_error();
}

void
Dbtoken::read_write_idproj(const char *in_path, const char *out_path)
{
	ifstream in(in_path);
	verify_open(out_path, in);

	ofstream out(out_path);
	verify_open(out_path, out);

	// Avoid duplicate entries, keyed by fid, foffset, len
	set<tuple<Eclass *, int>> dumped;

	string line_record;
	line_number = 0;
	while (getline(in, line_record)) {
		line_number++;
		if (DP())
			cout << in_path << '(' << line_number << "): " << line_record << '\n';

		istringstream line_stream(line_record);
		int pid;
		int fileid;
		unsigned long offset;
		int len;
		if (!(line_stream >> fileid >> offset >> len >> pid)) {
			issue_error(in_path, line_record);
			continue;
		}


		Tokid ti(Fileid(fileid), offset);
		int covered = 0;
		while (covered < len) {
			Eclass *ec = check_ec(ti);

			if (ec == NULL) {
				issue_error(in_path, "Obtain idproj EC", ti);
				goto next_line;
			}

			auto key = std::make_tuple(ec, pid);
			if (dumped.find(key) == dumped.end()) {
				out << ptr_offset(ec) << ',' << pid << '\n';
				dumped.insert(key);
			} else {
				if (DP())
					cout << "Skip dumped entry " << ec << pid << '\n';
			}

			if (DP())
			    cout << "fileid: " << fileid
				<< " offset: " << offset
				<< " len: " << len
				<< " EC len: " << ec->get_len()
				<< " covered: " << covered
				<< " pid: " << pid
				<< '\n';


			covered += ec->get_len();
			ti += ec->get_len();
		}
next_line:
		continue;
	}
	exit_on_error();
}
