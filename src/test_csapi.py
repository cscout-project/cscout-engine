#!/usr/bin/env python3
#
# (C) Copyright 2026 Diomidis Spinellis
# (C) Copyright 2026 Ujjwal Aggarwal
#
# This file is part of CScout.
#
# CScout is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# CScout is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with CScout.  If not, see <http://www.gnu.org/licenses/>.
#

import json
import os
import sqlite3
import subprocess
import tempfile
import textwrap
import threading
import unittest
import urllib.request
import urllib.error
from http.server import HTTPServer

import csapi


class TestCsapi(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.db_fd, cls.db_path = tempfile.mkstemp()
        os.close(cls.db_fd)
        csapi._db_path = cls.db_path

        base_dir = os.path.dirname(os.path.abspath(__file__))
        cscout_bin = os.environ.get("CSCOUT", os.path.join(base_dir, "build", "cscout"))

        cls.c_file_path = os.path.join(base_dir, "test", "c", "test_csapi_sample.c")
        with open(cls.c_file_path, "w") as f:
            f.write(textwrap.dedent("""\
                int sample_global_var = 1;
                void sample_global_func(void) {
                }
                #define SAMPLE_MACRO 42
                static void sample_static_func(void) {
                    sample_global_func();
                }
            """))

        cls.ws_spec_path = os.path.join(base_dir, "test_ws.ws")
        with open(cls.ws_spec_path, "w") as f:
            f.write(textwrap.dedent("""\
                workspace TestWS {
                    ipath "../include/stdc"
                    directory test/c {
                        project TestProject {
                            file test_csapi_sample.c
                        }
                    }
                }
            """))

        cls.project_cs_path = os.path.join(base_dir, "test_project.cs")
        cswc_res = subprocess.run(
            ["perl", "cswc.pl", "-d", "../example/.cscout", "test_ws.ws"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            cwd=base_dir
        )
        if cswc_res.returncode != 0:
            raise RuntimeError(f"cswc.pl failed: {cswc_res.stderr}")

        with open(cls.project_cs_path, "w") as f:
            f.write(cswc_res.stdout)

        cscout_res = subprocess.run(
            [cscout_bin, "-s", "sqlite", "test_project.cs"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            cwd=base_dir
        )
        if cscout_res.returncode != 0:
            raise RuntimeError(f"cscout failed: {cscout_res.stderr}")

        conn = sqlite3.connect(cls.db_path)
        conn.executescript(cscout_res.stdout)

        cursor = conn.cursor()
        cursor.execute("SELECT EID FROM IDS WHERE NAME = 'sample_global_var'")
        cls.eid_global_var = cursor.fetchone()[0]
        cursor.execute("SELECT EID FROM IDS WHERE NAME = 'sample_global_func'")
        cls.eid_global_func = cursor.fetchone()[0]
        cursor.execute("SELECT EID FROM IDS WHERE NAME = 'SAMPLE_MACRO'")
        cls.eid_sample_macro = cursor.fetchone()[0]
        cursor.execute("SELECT EID FROM IDS WHERE NAME = 'sample_static_func'")
        cls.eid_static_func = cursor.fetchone()[0]

        cursor.execute("SELECT FID FROM FILES WHERE NAME LIKE '%test_csapi_sample.c'")
        cls.fid_sample = cursor.fetchone()[0]

        cursor.execute("SELECT ID FROM FUNCTIONS WHERE NAME = 'sample_global_func'")
        cls.fnid_global_func = cursor.fetchone()[0]
        cursor.execute("SELECT ID FROM FUNCTIONS WHERE NAME = 'sample_static_func'")
        cls.fnid_static_func = cursor.fetchone()[0]

        conn.close()

        cls.server = HTTPServer(("127.0.0.1", 0), csapi.Handler)
        cls.port = cls.server.server_address[1]
        cls.server_thread = threading.Thread(target=cls.server.serve_forever)
        cls.server_thread.daemon = True
        cls.server_thread.start()

    @classmethod
    def tearDownClass(cls):
        # Hit /quit first because the server checks an internal flag on that
        # endpoint; server.shutdown() alone does not trigger it.
        try:
            req = urllib.request.Request(f"http://127.0.0.1:{cls.port}/quit")
            with urllib.request.urlopen(req) as response:
                pass
        except Exception:
            pass

        try:
            cls.server.shutdown()
            cls.server_thread.join(timeout=2)
        except Exception:
            pass

        for path in (cls.db_path, cls.c_file_path, cls.ws_spec_path, cls.project_cs_path):
            try:
                os.remove(path)
            except Exception:
                pass

    def get_url(self, path):
        return f"http://127.0.0.1:{self.port}{path}"

    def get_json(self, path):
        req = urllib.request.Request(self.get_url(path))
        with urllib.request.urlopen(req) as response:
            self.assertEqual(response.status, 200)
            return json.loads(response.read().decode('utf-8'))

    def test_status(self):
        data = self.get_json("/status")
        self.assertEqual(data["status"], "ok")
        self.assertTrue(data["indexes_ready"])

    def test_identifiers(self):
        data = self.get_json("/identifiers")
        self.assertGreater(len(data), 0)

        data_unused = self.get_json("/identifiers?unused=1")
        self.assertTrue(any(r["NAME"] == "sample_global_var" for r in data_unused))

        data_search = self.get_json("/identifiers?name=sample_global_func")
        self.assertEqual(len(data_search), 1)
        self.assertEqual(data_search[0]["NAME"], "sample_global_func")

        data_paged = self.get_json("/identifiers?limit=1&offset=1")
        self.assertEqual(len(data_paged), 1)

    def test_identifier(self):
        data = self.get_json(f"/identifier?eid={self.eid_global_var}")
        self.assertEqual(data["identifier"]["NAME"], "sample_global_var")
        self.assertEqual(len(data["locations"]), 1)
        self.assertEqual(data["locations"][0]["LNUM"], 1)

        with self.assertRaises(urllib.error.HTTPError) as cm:
            self.get_json("/identifier?eid=999999999")
        self.assertEqual(cm.exception.code, 404)

        with self.assertRaises(urllib.error.HTTPError) as cm:
            self.get_json("/identifier")
        self.assertEqual(cm.exception.code, 400)

    def test_files(self):
        data = self.get_json("/files")
        self.assertTrue(any(f["NAME"].endswith("test_csapi_sample.c") for f in data))

    def test_filemetrics(self):
        data = self.get_json(f"/filemetrics?fid={self.fid_sample}")
        self.assertEqual(len(data), 2)
        self.assertIn("NSTMT", data[0])

    def test_functions(self):
        data = self.get_json("/functions")
        self.assertTrue(any(f["NAME"] == "sample_global_func" for f in data))
        self.assertTrue(any(f["NAME"] == "sample_static_func" for f in data))

        data_scoped = self.get_json("/functions?filescoped=1")
        self.assertTrue(any(f["NAME"] == "sample_static_func" for f in data_scoped))

    def test_funmetrics(self):
        data = self.get_json(f"/funmetrics?fnid={self.fnid_static_func}")
        self.assertEqual(len(data), 2)
        post_cpp = next(r for r in data if r["PRECPP"] == 0)
        self.assertEqual(post_cpp["FANOUT"], 1)

    def test_callers(self):
        data = self.get_json(f"/callers?fnid={self.fnid_global_func}")
        self.assertTrue(any(f["NAME"] == "sample_static_func" for f in data))

    def test_callees(self):
        data = self.get_json(f"/callees?fnid={self.fnid_static_func}")
        self.assertTrue(any(f["NAME"] == "sample_global_func" for f in data))

    def test_projects(self):
        data = self.get_json("/projects")
        self.assertTrue(any(p["NAME"] == "TestProject" for p in data))

    def test_rename_preview(self):
        data = self.get_json(f"/rename/preview?eid={self.eid_global_var}&newname=renamed_var")
        self.assertEqual(data["eid"], self.eid_global_var)
        self.assertEqual(data["old_name"], "sample_global_var")
        self.assertEqual(data["new_name"], "renamed_var")
        self.assertEqual(data["total_replacements"], 1)
        self.assertEqual(len(data["locations"]), 1)


    def test_attributes_identifiers(self):
        data = self.get_json("/attributes/identifiers")
        self.assertGreater(len(data), 0)
        self.assertTrue(any(a["id"] == "READONLY" for a in data))
        self.assertTrue(any(a["id"] == "FUN" for a in data))

    def test_attributes_files(self):
        data = self.get_json("/attributes/files")
        self.assertGreater(len(data), 0)
        self.assertTrue(any(a["id"] == "NLINE" for a in data))
        self.assertTrue(any(a["id"] == "NSTMT" for a in data))


class TestCsapiHelpers(unittest.TestCase):
    def test_rows_to_list(self):
        conn = sqlite3.connect(":memory:")
        conn.row_factory = sqlite3.Row
        cursor = conn.cursor()
        cursor.execute("CREATE TABLE t (a INT, b TEXT)")
        cursor.execute("INSERT INTO t VALUES (1, 'hello')")
        row = cursor.execute("SELECT * FROM t").fetchone()
        res = csapi.rows_to_list([row])
        self.assertEqual(res, [{"a": 1, "b": "hello"}])
        conn.close()

    def test_get_param(self):
        qs = {"a": ["first", "second"], "b": []}
        self.assertEqual(csapi.get_param(qs, "a"), "first")
        self.assertEqual(csapi.get_param(qs, "b"), None)
        self.assertEqual(csapi.get_param(qs, "c", "default_val"), "default_val")

    def test_get_bool_param(self):
        qs = {
            "a": ["1"],
            "b": ["true"],
            "c": ["yes"],
            "d": ["0"],
            "e": ["false"],
            "f": ["other"]
        }
        self.assertTrue(csapi.get_bool_param(qs, "a"))
        self.assertTrue(csapi.get_bool_param(qs, "b"))
        self.assertTrue(csapi.get_bool_param(qs, "c"))
        self.assertFalse(csapi.get_bool_param(qs, "d"))
        self.assertFalse(csapi.get_bool_param(qs, "e"))
        self.assertFalse(csapi.get_bool_param(qs, "f"))
        self.assertIsNone(csapi.get_bool_param(qs, "missing"))

    def test_get_int_param(self):
        qs = {"a": ["42"], "b": ["not_an_int"]}
        self.assertEqual(csapi.get_int_param(qs, "a", 10), 42)
        self.assertEqual(csapi.get_int_param(qs, "b", 10), 10)
        self.assertEqual(csapi.get_int_param(qs, "missing", 10), 10)

    def test_get_required_param(self):
        qs = {"a": ["hello"]}
        self.assertEqual(csapi.get_required_param(qs, "a"), "hello")
        with self.assertRaises(csapi.MissingParameterError):
            csapi.get_required_param(qs, "missing")

    def test_get_required_int_param(self):
        qs = {"a": ["42"], "b": ["not_an_int"]}
        self.assertEqual(csapi.get_required_int_param(qs, "a"), 42)
        with self.assertRaises(csapi.MissingParameterError):
            csapi.get_required_int_param(qs, "b")
        with self.assertRaises(csapi.MissingParameterError):
            csapi.get_required_int_param(qs, "missing")

    def test_get_paging_params(self):
        qs = {"limit": ["10"], "offset": ["20"]}
        limit, offset = csapi.get_paging_params(qs)
        self.assertEqual(limit, 10)
        self.assertEqual(offset, 20)

        limit, offset = csapi.get_paging_params({})
        self.assertEqual(limit, 1000)
        self.assertEqual(offset, 0)

    def test_build_where_clause(self):
        qs = {
            "unused": ["1"],
            "name": ["func"],
            "missing": ["123"]
        }
        filters = [
            ("unused", "UNUSED = ?", "bool"),
            ("macro", "MACRO = ?", "bool"),
            ("name", "NAME LIKE ?", "like"),
        ]
        conditions, params = csapi.build_where_clause(qs, filters)
        self.assertEqual(conditions, ["UNUSED = ?", "NAME LIKE ?"])
        self.assertEqual(params, [1, "%func%"])


if __name__ == "__main__":
    unittest.main()
