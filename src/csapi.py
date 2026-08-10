#!/usr/bin/env python3
#
# (C) Copyright 2008-2026 Diomidis Spinellis
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
#
# csapi.py -- REST API server for querying a CScout SQLite database.
#
# After running:
#   cscout -s sqlite project.cs | sqlite3 project.db
# start this server with:
#   csapi project.db
#
# The server listens on localhost:8081 by default and exposes
# CScout analysis results as JSON.
#
# No external dependencies -- uses only the Python standard library.
#

import argparse
import json
import os
import sqlite3
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlparse

class MissingParameterError(ValueError):
    """Exception raised when a required query parameter is missing or invalid."""
    pass

SAVED_QUERIES_FILES = {
    "writable": {"where": "f.RO = 0"},
    "readonly": {"where": "f.RO = 1"},
    "with_unused": {
        "join": "JOIN TOKENS t ON t.FID = f.FID JOIN IDS i ON i.EID = t.EID",
        "where": "i.UNUSED = 1 AND i.READONLY = 0",
        "distinct": True
    },
    "no_statements": {
        "join": "JOIN FILEMETRICS fm ON fm.FID = f.FID",
        "where": "fm.PRECPP = 0 AND f.NAME LIKE '%.c' AND (fm.NSTMT = 0 OR fm.NSTMT IS NULL)"
    },
    "unprocessed": {
        "join": "JOIN FILEMETRICS fm ON fm.FID = f.FID",
        "where": "fm.PRECPP = 0 AND fm.NULINE > 0"
    },
    "with_strings": {
        "join": "JOIN FILEMETRICS fm ON fm.FID = f.FID",
        "where": "fm.PRECPP = 0 AND fm.NSTRING > 0"
    },
    "h_with_includes": {
        "join": "JOIN FILEMETRICS fm ON fm.FID = f.FID",
        "where": "f.NAME LIKE '%.h' AND fm.PRECPP = 1 AND fm.NINCFILE > 0"
    }
}

SAVED_QUERIES_IDS = {
    "readonly": {"where": "i.READONLY = 1 AND i.MACROARG = 0"},
    "writable": {"where": "i.READONLY = 0 AND i.MACROARG = 0"},
    "unused_project": {"where": "i.UNUSED = 1 AND i.LSCOPE = 1 AND i.READONLY = 0 AND i.MACROARG = 0"},
    "unused_file": {"where": "i.UNUSED = 1 AND i.CSCOPE = 1 AND i.READONLY = 0 AND i.MACROARG = 0"},
    "unused_macros": {"where": "i.UNUSED = 1 AND i.MACRO = 1 AND i.READONLY = 0 AND i.MACROARG = 0"},
    "file_spanning": {"where": "i.READONLY = 0 AND i.EID IN (SELECT EID FROM TOKENS GROUP BY EID HAVING COUNT(DISTINCT FID) > 1)"},
    "static_vars": {"where": "i.READONLY = 0 AND i.ORDINARY = 1 AND i.LSCOPE = 1 AND i.NAME != 'main' AND i.EID NOT IN (SELECT EID FROM TOKENS GROUP BY EID HAVING COUNT(DISTINCT FID) > 1)"},
    "static_funs": {"where": "i.FUN = 1 AND i.READONLY = 0 AND i.ORDINARY = 1 AND i.LSCOPE = 1 AND i.NAME != 'main' AND i.EID NOT IN (SELECT EID FROM TOKENS GROUP BY EID HAVING COUNT(DISTINCT FID) > 1)"},
}

SAVED_QUERIES_FUNCTIONS = {
    "project_scoped": {"where": "f.FILESCOPED = 0 AND f.ISMACRO = 0"},
    "file_scoped": {"where": "f.FILESCOPED = 1 AND f.ISMACRO = 0"},
    "not_called": {"where": "f.FANIN = 0 AND f.ISMACRO = 0"},
    "called_once": {"where": "f.FANIN = 1 AND f.ISMACRO = 0"},
}


def ensure_index(conn: sqlite3.Connection, table: str, columns: list) -> None:
    """Ensure that an index exists on the specified table and columns."""
    index_name = f"idx_{table.lower()}_{'_'.join(columns).lower()}"
    columns_str = ", ".join(columns)
    conn.execute(f"CREATE INDEX IF NOT EXISTS {index_name} ON {table}({columns_str})")


def get_db() -> sqlite3.Connection:
    """Open and return a new database connection."""
    conn = sqlite3.connect(_db_path, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    return conn


def rows_to_list(rows) -> list:
    """Convert SQLite Row objects into standard Python dictionaries."""
    return [dict(r) for r in rows]


def get_param(qs, name, default=None):
    """Get a single string query parameter value by name."""
    vals = qs.get(name)
    return vals[0] if vals else default


def get_bool_param(qs, name):
    """Get a query parameter value and convert it to a boolean."""
    v = get_param(qs, name)
    if v is None:
        return None
    return v.lower() in ("1", "true", "yes")


def get_int_param(qs, name, default):
    """Get a query parameter value and convert it to an integer."""
    v = get_param(qs, name)
    try:
        return int(v) if v is not None else default
    except ValueError:
        return default


def get_required_param(qs, name) -> str:
    """Get a single string query parameter or raise MissingParameterError."""
    v = get_param(qs, name)
    if v is None:
        raise MissingParameterError(f"{name} parameter required")
    return v


def get_required_int_param(qs, name) -> int:
    """Get a query parameter converted to an integer, or raise MissingParameterError."""
    v = get_param(qs, name)
    if v is None:
        raise MissingParameterError(f"{name} parameter required")
    try:
        return int(v)
    except ValueError:
        raise MissingParameterError(f"{name} parameter must be an integer")


def get_paging_params(qs) -> tuple:
    """Get limit and offset parameters for pagination."""
    return get_int_param(qs, "limit", 1000), get_int_param(qs, "offset", 0)


def build_where_clause(qs, filters) -> tuple:
    """Build dynamic WHERE conditions and query parameters from a table of filters.

    filters is a list of tuples: (param_name, column_name, filter_type)
    where filter_type is 'bool', 'str', or 'like'.
    """
    conditions = []
    params = []
    for param_name, sql_cond, param_type in filters:
        if param_type == "bool":
            val = get_bool_param(qs, param_name)
            if val is not None:
                conditions.append(sql_cond)
                params.append(int(val))
        elif param_type == "str":
            val = get_param(qs, param_name)
            if val:
                conditions.append(sql_cond)
                params.append(val)
        elif param_type == "like":
            val = get_param(qs, param_name)
            if val:
                conditions.append(sql_cond)
                params.append(f"%{val}%")
        elif param_type == "int":
            val = get_int_param(qs, param_name, None)
            if val is not None:
                conditions.append(sql_cond)
                params.append(val)
    return conditions, params


class Handler(BaseHTTPRequestHandler):
    """Custom HTTP request handler for the CScout REST API endpoints."""

    def log_message(self, fmt, *args):
        """Suppress per-request access log output."""
        pass

    def send_json(self, data, status=200):
        """Send a JSON response with CORS headers."""
        body = json.dumps(data).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def send_error_json(self, status, message):
        """Send a JSON formatted error response with the specified status code."""
        self.send_json({"error": message}, status)

    def do_GET(self):
        """Parse query string parameters and route GET requests to the endpoints."""
        parsed = urlparse(self.path)
        path = parsed.path
        qs = parse_qs(parsed.query)

        conn = get_db()

        try:
            if path == "/status":
                self.handle_status(conn, qs)
            elif path == "/quit":
                self.handle_quit()
            elif path == "/identifiers":
                self.handle_identifiers(conn, qs)
            elif path == "/identifier":
                self.handle_identifier(conn, qs)
            elif path == "/attributes/identifiers":
                self.handle_attributes_identifiers(conn, qs)
            elif path == "/attributes/files":
                self.handle_attributes_files(conn, qs)
            elif path == "/files":
                self.handle_files(conn, qs)
            elif path == "/filegraph/include":
                self.handle_filegraph_include(conn, qs)
            elif path == "/filegraph/compile":
                self.handle_filegraph_compile(conn, qs)
            elif path == "/filegraph/control":
                self.handle_filegraph_control(conn, qs)
            elif path == "/filegraph/data":
                self.handle_filegraph_data(conn, qs)
            elif path == "/filemetrics":
                self.handle_filemetrics(conn, qs)
            elif path == "/file/detail":
                self.handle_file_detail(conn, qs)
            elif path == "/functions":
                self.handle_functions(conn, qs)
            elif path == "/funmetrics":
                self.handle_funmetrics(conn, qs)
            elif path == "/callers":
                self.handle_callers(conn, qs)
            elif path == "/callees":
                self.handle_callees(conn, qs)
            elif path == "/projects":
                self.handle_projects(conn, qs)
            elif path == "/rename/preview":
                self.handle_rename_preview(conn, qs)
            elif path == "/rename/apply":
                self.handle_rename_apply(conn, qs)
            elif path == "/identifier/detail":
                self.handle_identifier_detail(conn, qs)
            elif path == "/filemetrics/aggregate":
                self.handle_filemetrics_aggregate(conn, qs)
            elif path == "/funmetrics/aggregate":
                self.handle_funmetrics_aggregate(conn, qs)
            elif path == "/project/files":
                self.handle_project_files(conn, qs)
            elif path == "/callgraph":
                self.handle_callgraph(conn, qs)
            elif path == "/identifiers/counts":
                self.handle_identifiers_counts(conn, qs)
            elif path == "/functions/counts":
                self.handle_functions_counts(conn, qs)
            elif path == "/functions/byname":
                self.handle_functions_byname(conn, qs)
            elif path == "/files/counts":
                self.handle_files_counts(conn, qs)
            else:
                self.send_error_json(404, f"Unknown endpoint: {path}")
        except MissingParameterError as e:
            self.send_error_json(400, str(e))
        except Exception as e:
            self.send_error_json(500, str(e))
        finally:
            conn.close()

    def handle_status(self, conn, qs):
        """Handle /status requests."""
        self.send_json({
            "status": "ok",
            "indexes_ready": True,
        })

    def handle_quit(self):
        """Handle /quit requests to shut down the server gracefully."""
        self.send_json({"status": "quit"})
        # Shut down the server in a separate thread to avoid deadlock.
        threading.Thread(target=self.server.shutdown).start()

    def handle_identifiers(self, conn, qs):
        """Handle /identifiers requests."""
        ensure_index(conn, "IDS", ["NAME"])
        query = get_param(qs, "query")

        filters = [
            ("name", "i.NAME LIKE ?", "like"),
            ("name_ne", "i.NAME != ?", "str"),
        ]
        conditions, params = build_where_clause(qs, filters)

        joins = []
        distinct = False

        if query and query in SAVED_QUERIES_IDS:
            sq = SAVED_QUERIES_IDS[query]
            if "join" in sq:
                joins.append(sq["join"])
            if "where" in sq:
                conditions.append(sq["where"])
            if "distinct" in sq:
                distinct = True

        limit, offset = get_paging_params(qs)

        join_str = " ".join(joins)
        where = ("WHERE " + " AND ".join(conditions)) if conditions else ""
        params += [limit, offset]
        
        distinct_str = "DISTINCT " if distinct else ""

        rows = conn.execute(
            f"SELECT {distinct_str}i.* FROM IDS i {join_str} {where} LIMIT ? OFFSET ?",
            params).fetchall()
        self.send_json(rows_to_list(rows))

    def handle_identifiers_counts(self, conn, qs):
        """Handle /identifiers/counts — returns COUNT(*) per category in one query.

        This avoids downloading thousands of rows just to show folder counts in
        the Identifiers sidebar panel.
        """
        ensure_index(conn, "TOKENS", ["EID"])
        # Main counts from IDS (single pass)
        row = conn.execute("""
            SELECT
              COUNT(*) AS all_ids,
              SUM(CASE WHEN READONLY=1 AND MACROARG=0 THEN 1 ELSE 0 END) AS readonly,
              SUM(CASE WHEN READONLY=0 AND MACROARG=0 THEN 1 ELSE 0 END) AS writable,
              SUM(CASE WHEN UNUSED=1 AND LSCOPE=1 AND READONLY=0 AND MACROARG=0 THEN 1 ELSE 0 END) AS unused_project,
              SUM(CASE WHEN UNUSED=1 AND CSCOPE=1 AND READONLY=0 AND MACROARG=0 THEN 1 ELSE 0 END) AS unused_file,
              SUM(CASE WHEN UNUSED=1 AND MACRO=1 AND READONLY=0 AND MACROARG=0 THEN 1 ELSE 0 END) AS unused_macros
            FROM IDS
        """).fetchone()
        # File-spanning count (needs TOKENS join)
        file_spanning = conn.execute("""
            SELECT COUNT(*) FROM IDS
            WHERE READONLY=0
            AND EID IN (SELECT EID FROM TOKENS GROUP BY EID HAVING COUNT(DISTINCT FID) > 1)
        """).fetchone()[0]
        # Should-be-static: vars vs funs
        static_row = conn.execute("""
            SELECT
              SUM(CASE WHEN ORDINARY=1 AND FUN=0 THEN 1 ELSE 0 END) AS static_vars,
              SUM(CASE WHEN FUN=1 THEN 1 ELSE 0 END) AS static_funs
            FROM IDS
            WHERE READONLY=0 AND ORDINARY=1 AND LSCOPE=1 AND NAME != 'main'
            AND EID NOT IN (SELECT EID FROM TOKENS GROUP BY EID HAVING COUNT(DISTINCT FID) > 1)
        """).fetchone()
        self.send_json({
            "all": row["all_ids"],
            "readonly": row["readonly"],
            "writable": row["writable"],
            "file_spanning": file_spanning,
            "unused_project": row["unused_project"],
            "unused_file": row["unused_file"],
            "unused_macros": row["unused_macros"],
            "static_vars": static_row["static_vars"] or 0,
            "static_funs": static_row["static_funs"] or 0,
        })

    def handle_identifier(self, conn, qs):
        """Handle /identifier requests."""
        ensure_index(conn, "TOKENS", ["EID"])
        ensure_index(conn, "LINEPOS", ["FID", "FOFFSET"])
        eid = get_required_int_param(qs, "eid")
        row = conn.execute(
            "SELECT * FROM IDS WHERE EID = ?",
            (eid,)).fetchone()
        if row is None:
            self.send_error_json(404, "Identifier not found")
            return

        limit, offset = get_paging_params(qs)
        tokens = conn.execute(
            """
            SELECT t.FID, f.NAME AS FILE, t.FOFFSET, l.LNUM, f.RO AS RO
            FROM TOKENS t
            JOIN FILES f ON t.FID = f.FID
            LEFT JOIN LINEPOS l
                   ON l.FID = t.FID AND l.FOFFSET = (
                       SELECT MAX(FOFFSET) FROM LINEPOS
                       WHERE FID = t.FID AND FOFFSET <= t.FOFFSET)
            WHERE t.EID = ?
            ORDER BY t.FID, t.FOFFSET
            LIMIT ? OFFSET ?
            """,
            (eid, limit, offset)).fetchall()
        self.send_json({
            "identifier": dict(row),
            "locations": rows_to_list(tokens),
        })

    def handle_files(self, conn, qs):
        """Handle /files requests with optional filters.

        Parameters mirror the CScout web file query (xfilequery.html):
        writable=1       — writable files only (RO=0)
        ro=1             — read-only files only (RO=1)
        fre=<regex>      — filter filenames matching SQL LIKE pattern
        has_unused=1     — files containing unused writable identifiers
        no_statements=1  — writable .c files with no statements (NSTMT=0)
        unprocessed=1    — files with unprocessed lines (NULINE>0)
        has_strings=1    — files containing string literals (NSTRING>0)
        h_with_includes=1 — writable .h files with #include directives
        """
        query = get_param(qs, "query")

        conditions = []
        params = []
        joins = []
        distinct = False

        fre = get_param(qs, "fre")
        if fre:
            conditions.append("f.NAME LIKE ?")
            params.append(f"%{fre}%")

        if query and query in SAVED_QUERIES_FILES:
            sq = SAVED_QUERIES_FILES[query]
            if "join" in sq:
                joins.append(sq["join"])
            if "where" in sq:
                conditions.append(sq["where"])
            if "distinct" in sq:
                distinct = True

        join_str = " ".join(joins)
        where = ("WHERE " + " AND ".join(conditions)) if conditions else ""

        distinct_str = "DISTINCT " if distinct else ""

        rows = conn.execute(
            f"SELECT {distinct_str}f.FID, f.NAME, f.RO "
            f"FROM FILES f {join_str} {where} ORDER BY f.NAME",
            params).fetchall()
        self.send_json(rows_to_list(rows))

    def handle_filemetrics(self, conn, qs):
        """Handle /filemetrics requests."""
        ensure_index(conn, "FILEMETRICS", ["FID"])
        fid = get_required_int_param(qs, "fid")
        rows = conn.execute(
            "SELECT * FROM FILEMETRICS WHERE FID = ?",
            (fid,)).fetchall()
        if not rows:
            self.send_error_json(404, "File not found")
            return
        self.send_json(rows_to_list(rows))

    def handle_file_detail(self, conn, qs):
        """Handle /file/detail requests.
        Returns file attributes, metrics, functions defined in it,
        files it includes, and files that include it.
        """
        fid = get_required_int_param(qs, "fid")
        file_row = conn.execute(
            "SELECT * FROM FILES WHERE FID = ?",
            (fid,)).fetchone()
        if file_row is None:
            self.send_error_json(404, "File not found")
            return
        metrics = conn.execute(
            "SELECT * FROM FILEMETRICS WHERE FID = ? AND PRECPP = 0",
            (fid,)).fetchone()
        functions = conn.execute(
            """SELECT f.ID, f.NAME, f.FANIN, fm.FANOUT, fm.CCYCL1,
                      lp.LNUM
               FROM FUNCTIONS f
               LEFT JOIN FUNCTIONMETRICS fm
                      ON fm.FUNCTIONID = f.ID AND fm.PRECPP = 0
               LEFT JOIN LINEPOS lp
                      ON lp.FID = f.FID AND lp.FOFFSET = (
                          SELECT MAX(FOFFSET) FROM LINEPOS
                          WHERE FID = f.FID AND FOFFSET <= f.FOFFSET)
               WHERE f.FID = ? AND f.DEFINED = 1
               ORDER BY lp.LNUM""",
            (fid,)).fetchall()
        includes = conn.execute(
            """SELECT DISTINCT f.FID, f.NAME, f.RO
               FROM INCLUDERS i
               JOIN FILES f ON i.BASEFILEID = f.FID
               WHERE i.INCLUDERID = ?
               ORDER BY f.NAME""",
            (fid,)).fetchall()
        included_by = conn.execute(
            """SELECT DISTINCT f.FID, f.NAME, f.RO
               FROM INCLUDERS i
               JOIN FILES f ON i.INCLUDERID = f.FID
               WHERE i.BASEFILEID = ?
               ORDER BY f.NAME""",
            (fid,)).fetchall()
        self.send_json({
            "file": dict(file_row),
            "metrics": dict(metrics) if metrics else {},
            "functions": rows_to_list(functions),
            "includes": rows_to_list(includes),
            "included_by": rows_to_list(included_by),
        })

    def handle_functions(self, conn, qs):
        """Handle /functions requests."""
        ensure_index(conn, "LINEPOS", ["FID", "FOFFSET"])
        query = get_param(qs, "query")

        filters = [
            ("name", "f.NAME LIKE ?", "like"),
        ]
        conditions, params = build_where_clause(qs, filters)

        conditions.append("f.DEFINED = 1")
        distinct = False

        if query and query in SAVED_QUERIES_FUNCTIONS:
            sq = SAVED_QUERIES_FUNCTIONS[query]
            if "where" in sq:
                conditions.append(sq["where"])
            if "distinct" in sq:
                distinct = True

        limit, offset = get_paging_params(qs)

        where = ("WHERE " + " AND ".join(conditions)) if conditions else ""
        params += [limit, offset]
        
        distinct_str = "DISTINCT " if distinct else ""

        rows = conn.execute(
            f"""SELECT {distinct_str}f.ID, f.NAME, f.ISMACRO, f.DEFINED, f.DECLARED,
                      f.FILESCOPED, f.FID, f.FOFFSET, f.FANIN,
                      fm.FANOUT, fm.CCYCL1, fi.NAME AS FILE, l.LNUM
               FROM FUNCTIONS f
               LEFT JOIN FUNCTIONMETRICS fm
                      ON fm.FUNCTIONID = f.ID AND fm.PRECPP = 0
               LEFT JOIN FILES fi ON fi.FID = f.FID
               LEFT JOIN LINEPOS l
                      ON l.FID = f.FID AND l.FOFFSET = (
                          SELECT MAX(FOFFSET) FROM LINEPOS
                          WHERE FID = f.FID AND FOFFSET <= f.FOFFSET)
               {where} LIMIT ? OFFSET ?""",
            params).fetchall()
        self.send_json(rows_to_list(rows))

    def handle_functions_counts(self, conn, qs):
        """Handle /functions/counts — returns COUNT(*) per sidebar category.

        Single SQL query using CASE WHEN to avoid multiple round-trips.
        Only counts DEFINED=1 functions (matching what the sidebar shows).
        """
        row = conn.execute("""
            SELECT
              COUNT(*) AS all_fns,
              SUM(CASE WHEN FILESCOPED=0 AND ISMACRO=0 THEN 1 ELSE 0 END) AS project_scoped,
              SUM(CASE WHEN FILESCOPED=1 AND ISMACRO=0 THEN 1 ELSE 0 END) AS file_scoped,
              SUM(CASE WHEN FANIN=0 AND ISMACRO=0 THEN 1 ELSE 0 END) AS not_called,
              SUM(CASE WHEN FANIN=1 AND ISMACRO=0 THEN 1 ELSE 0 END) AS called_once
            FROM FUNCTIONS
            WHERE DEFINED=1
        """).fetchone()
        self.send_json({
            "all": row["all_fns"],
            "project_scoped": row["project_scoped"],
            "file_scoped": row["file_scoped"],
            "not_called": row["not_called"],
            "called_once": row["called_once"],
        })

    def handle_functions_byname(self, conn, qs):
        """Handle /functions/byname — return a single function row by name.

        Used by the hover provider to fetch complexity metrics for one function
        without downloading the entire function list (which was 10,000+ rows).
        Returns the first matching defined function with its metrics.
        """
        ensure_index(conn, "LINEPOS", ["FID", "FOFFSET"])
        name = get_required_param(qs, "name")
        row = conn.execute(
            """SELECT f.ID, f.NAME, f.ISMACRO, f.DEFINED, f.DECLARED, f.FILESCOPED,
                      f.FID, f.FOFFSET, f.FANIN, fm.FANOUT, fm.CCYCL1,
                      fi.NAME AS FILE, l.LNUM
               FROM FUNCTIONS f
               LEFT JOIN FUNCTIONMETRICS fm ON fm.FUNCTIONID = f.ID AND fm.PRECPP = 0
               LEFT JOIN FILES fi ON fi.FID = f.FID
               LEFT JOIN LINEPOS l ON l.FID = f.FID AND l.FOFFSET = (
                   SELECT MAX(FOFFSET) FROM LINEPOS WHERE FID = f.FID AND FOFFSET <= f.FOFFSET)
               WHERE f.NAME = ? AND f.DEFINED = 1
               LIMIT 1""",
            (name,)).fetchone()
        if row is None:
            self.send_json(None)
        else:
            self.send_json(dict(row))

    def handle_files_counts(self, conn, qs):
        """Handle /files/counts — returns COUNT(*) per file sidebar category."""
        rows = conn.execute("""
            SELECT 'all' AS name, COUNT(*) AS value FROM FILES
            UNION
            SELECT 'readonly', COUNT(*) FROM FILES WHERE RO=1
            UNION
            SELECT 'writable', COUNT(*) FROM FILES WHERE RO=0
            UNION
            SELECT 'with_unused', COUNT(DISTINCT f.FID) FROM FILES f
                JOIN TOKENS t ON t.FID = f.FID
                JOIN IDS i ON i.EID = t.EID
                WHERE f.RO=0 AND i.UNUSED=1 AND i.READONLY=0
            UNION
            SELECT 'no_statements', COUNT(*) FROM FILES f
                JOIN FILEMETRICS fm ON fm.FID = f.FID
                WHERE f.RO=0 AND f.NAME LIKE '%.c' AND fm.PRECPP=0
                AND (fm.NSTMT=0 OR fm.NSTMT IS NULL)
            UNION
            SELECT 'unprocessed', COUNT(*) FROM FILES f
                JOIN FILEMETRICS fm ON fm.FID = f.FID
                WHERE f.RO=0 AND fm.PRECPP=0 AND fm.NULINE > 0
            UNION
            SELECT 'with_strings', COUNT(*) FROM FILES f
                JOIN FILEMETRICS fm ON fm.FID = f.FID
                WHERE f.RO=0 AND fm.PRECPP=0 AND fm.NSTRING > 0
            UNION
            SELECT 'h_with_includes', COUNT(DISTINCT f.FID) FROM FILES f
                JOIN FILEMETRICS fm ON fm.FID = f.FID
                WHERE f.RO=0 AND f.NAME LIKE '%.h'
                AND fm.PRECPP=1 AND fm.NINCFILE > 0
        """).fetchall()
        self.send_json({row["name"]: row["value"] for row in rows})

    def handle_funmetrics(self, conn, qs):
        """Handle /funmetrics requests."""
        ensure_index(conn, "FUNCTIONMETRICS", ["FUNCTIONID"])
        fnid = get_required_int_param(qs, "fnid")
        rows = conn.execute(
            "SELECT * FROM FUNCTIONMETRICS WHERE FUNCTIONID = ?",
            (fnid,)).fetchall()
        if not rows:
            self.send_error_json(404, "Function not found")
            return
        self.send_json(rows_to_list(rows))

    def handle_callers(self, conn, qs):
        """Handle /callers requests."""
        ensure_index(conn, "FCALLS", ["DESTID"])
        fnid = get_required_int_param(qs, "fnid")
        rows = conn.execute(
            """
            SELECT f.ID, f.NAME, f.FID, f.FOFFSET,
                   fi.NAME AS FILE
            FROM FCALLS fc
            JOIN FUNCTIONS f ON fc.SOURCEID = f.ID
            JOIN FILES fi ON f.FID = fi.FID
            WHERE fc.DESTID = ?
            ORDER BY f.NAME
            """,
            (fnid,)).fetchall()
        self.send_json(rows_to_list(rows))

    def handle_callees(self, conn, qs):
        """Handle /callees requests."""
        ensure_index(conn, "FCALLS", ["SOURCEID"])
        fnid = get_required_int_param(qs, "fnid")
        rows = conn.execute(
            """
            SELECT f.ID, f.NAME, f.FID, f.FOFFSET,
                   fi.NAME AS FILE
            FROM FCALLS fc
            JOIN FUNCTIONS f ON fc.DESTID = f.ID
            JOIN FILES fi ON f.FID = fi.FID
            WHERE fc.SOURCEID = ?
            ORDER BY f.NAME
            """,
            (fnid,)).fetchall()
        self.send_json(rows_to_list(rows))

    def handle_projects(self, conn, qs):
        """Handle /projects requests."""
        rows = conn.execute(
            "SELECT * FROM PROJECTS").fetchall()
        self.send_json(rows_to_list(rows))

    def handle_rename_preview(self, conn, qs):
        """Handle /rename/preview requests."""
        ensure_index(conn, "TOKENS", ["EID"])
        ensure_index(conn, "LINEPOS", ["FID", "FOFFSET"])
        eid = get_required_int_param(qs, "eid")
        newname = get_required_param(qs, "newname")
        id_row = conn.execute(
            "SELECT NAME FROM IDS WHERE EID = ?",
            (eid,)).fetchone()
        if id_row is None:
            self.send_error_json(404, "Identifier not found")
            return
        tokens = conn.execute(
            """
            SELECT t.FID, f.NAME AS FILE, t.FOFFSET, l.LNUM, l.FOFFSET AS LINE_START_OFFSET, f.RO AS RO
            FROM TOKENS t
            JOIN FILES f ON t.FID = f.FID
            LEFT JOIN LINEPOS l
                   ON l.FID = t.FID AND l.FOFFSET = (
                       SELECT MAX(FOFFSET) FROM LINEPOS
                       WHERE FID = t.FID AND FOFFSET <= t.FOFFSET)
            WHERE t.EID = ? AND f.RO = 0
            ORDER BY t.FID, t.FOFFSET
            """,
            (eid,)).fetchall()
        self.send_json({
            "eid": eid,
            "old_name": id_row["NAME"],
            "new_name": newname,
            "total_replacements": len(tokens),
            "locations": rows_to_list(tokens),
        })

    def handle_identifier_detail(self, conn, qs):
        """Handle /identifier/detail requests.

        Returns full identifier attributes, occurrence count, projects,
        dependent files, associated functions, and function detail.
        """
        eid = get_required_int_param(qs, "eid")
        id_row = conn.execute(
            "SELECT * FROM IDS WHERE EID = ?",
            (eid,)).fetchone()
        if id_row is None:
            self.send_error_json(404, "Identifier not found")
            return

        projects = conn.execute(
            """SELECT p.PID, p.NAME FROM IDPROJ ip
               JOIN PROJECTS p ON ip.PID = p.PID
               WHERE ip.EID = ?""",
            (eid,)).fetchall()

        occ = conn.execute(
            "SELECT COUNT(*) AS CNT FROM TOKENS WHERE EID = ?",
            (eid,)).fetchone()

        dep_files = conn.execute(
            """SELECT DISTINCT f.FID, f.NAME, f.RO
               FROM TOKENS t JOIN FILES f ON t.FID = f.FID
               WHERE t.EID = ?
               ORDER BY f.NAME""",
            (eid,)).fetchall()

        # Functions whose name contains this identifier.
        assoc_fns = conn.execute(
            """SELECT DISTINCT fn.ID, fn.NAME,
                      fi_f.NAME AS FILE, fn.FOFFSET
               FROM FUNCTIONID fi
               JOIN FUNCTIONS fn ON fi.FUNCTIONID = fn.ID
               JOIN FILES fi_f ON fn.FID = fi_f.FID
               WHERE fi.FID IN (
                   SELECT FID FROM TOKENS WHERE EID = ?)
               ORDER BY fn.NAME""",
            (eid,)).fetchall()

        fn_detail = None
        if id_row["FUN"]:
            fn_row = conn.execute(
                "SELECT * FROM FUNCTIONS WHERE NAME = ?",
                (id_row["NAME"],)).fetchone()
            if fn_row:
                fn_id = fn_row["ID"]
                callers_count = conn.execute(
                    "SELECT COUNT(*) AS CNT FROM FCALLS WHERE DESTID = ?",
                    (fn_id,)).fetchone()["CNT"]
                callees_count = conn.execute(
                    "SELECT COUNT(*) AS CNT FROM FCALLS WHERE SOURCEID = ?",
                    (fn_id,)).fetchone()["CNT"]
                fn_def = conn.execute(
                    """SELECT fd.*, f.NAME AS FILE
                       FROM FUNCTIONDEFS fd
                       JOIN FILES f ON fd.FIDBEGIN = f.FID
                       WHERE fd.FUNCTIONID = ?""",
                    (fn_id,)).fetchone()
                fn_metrics = conn.execute(
                    "SELECT * FROM FUNCTIONMETRICS WHERE FUNCTIONID = ?",
                    (fn_id,)).fetchall()
                fn_detail = {
                    "function": dict(fn_row),
                    "callers_count": callers_count,
                    "callees_count": callees_count,
                    "definition": dict(fn_def) if fn_def else None,
                    "metrics": rows_to_list(fn_metrics),
                }

        locations = conn.execute(
            """SELECT t.FOFFSET, t.EID, f.FID, f.NAME AS FILE, f.RO,
                      (SELECT lp.LNUM FROM LINEPOS lp
                       WHERE lp.FID = t.FID AND lp.FOFFSET <= t.FOFFSET
                       ORDER BY lp.FOFFSET DESC LIMIT 1) AS LNUM
               FROM TOKENS t
               JOIN FILES f ON t.FID = f.FID
               WHERE t.EID = ?
               ORDER BY f.NAME, LNUM""",
            (eid,)).fetchall()
        self.send_json({
            "identifier": dict(id_row),
            "occurrences": occ["CNT"] if occ else 0,
            "locations": rows_to_list(locations),
            "projects": rows_to_list(projects),
            "dependent_files": rows_to_list(dep_files),
            "associated_functions": rows_to_list(assoc_fns),
            "function_detail": fn_detail,
        })

    def handle_attributes_identifiers(self, conn, qs):
        """Return identifier attribute names and DB column ids from METADATA."""
        row = conn.execute(
            "SELECT VALUE FROM METADATA WHERE KEY = 'IdentifierAttributes'"
        ).fetchone()
        if row is None:
            self.send_error_json(404, "IdentifierAttributes not found in METADATA")
            return
        names = row["VALUE"].split(',')
        ids = ["READONLY", "SUETAG", "SUMEMBER", "LABEL", "ORDINARY",
               "MACRO", "UNDEFMACRO", "UNDEFEDMACRO", "REDEFEDSAMEMACRO",
               "REDEFEDDIFFMACRO", "MACROARG", "CSCOPE", "LSCOPE",
               "TYPEDEF", "ENUM", "YACC", "FUN"]
        self.send_json([{"id": ids[i], "name": names[i]}
                        for i in range(min(len(ids), len(names)))])

    def handle_attributes_files(self, conn, qs):
        """Return file metric names and DB column ids from METADATA."""
        row = conn.execute(
            "SELECT VALUE FROM METADATA WHERE KEY = 'FileMetricFields'"
        ).fetchone()
        if row is None:
            self.send_error_json(404, "FileMetricFields not found in METADATA")
            return
        names = row["VALUE"].split(',')
        ids = ["NCHAR","NCCOMMENT","NSPACE","NLCOMMENT","NBCOMMENT",
               "NLINE","MAXLINELEN","MAXSTMTLEN","MAXSTMTNEST",
               "MAXBRACENEST","MAXBRACKNEST","BRACENEST","BRACKNEST",
               "NULINE","NPPDIRECTIVE","NPPCOND","NPPFMACRO","NPPOMACRO",
               "NTOKEN","NSTMT","NOP","NUOP","NNCONST","NCLIT","NSTRING",
               "NPPCONCATOP","NPPSTRINGOP","NIF","NELSE","NSWITCH","NCASE",
               "NDEFAULT","NBREAK","NFOR","NWHILE","NDO","NCONTINUE",
               "NGOTO","NRETURN","NASM","NTYPEOF","NPID","NFID","NMID",
               "NID","NUPID","NUFID","NUMID","NUID","NLABEL",
               "NMACROEXPANDTOKEN"]
        self.send_json([{"id": ids[i], "name": names[i]}
                        for i in range(min(len(ids), len(names)))])

    def handle_filemetrics_aggregate(self, conn, qs):
        """Handle /filemetrics/aggregate requests.

        Returns all file metrics rows for aggregate analysis.
        """
        rows = conn.execute("SELECT * FROM FILEMETRICS").fetchall()
        self.send_json(rows_to_list(rows))

    def handle_funmetrics_aggregate(self, conn, qs):
        """Handle /funmetrics/aggregate requests.

        Returns all function metrics rows for aggregate analysis.
        """
        rows = conn.execute("SELECT * FROM FUNCTIONMETRICS").fetchall()
        self.send_json(rows_to_list(rows))

    def handle_project_files(self, conn, qs):
        """Handle /project/files requests.

        Returns all files belonging to a specific project.
        """
        pid = get_required_int_param(qs, "pid")
        rows = conn.execute(
            """SELECT f.FID, f.NAME, f.RO
               FROM FILEPROJ fp
               JOIN FILES f ON fp.FID = f.FID
               WHERE fp.PID = ?
               ORDER BY f.NAME""",
            (pid,)).fetchall()
        self.send_json(rows_to_list(rows))

    def handle_callgraph(self, conn, qs):
        """Handle /callgraph requests.

        Generates a call graph SVG for a function using graphviz dot.
        Returns the SVG as text/html for display in a webview.
        """

        fnid = get_required_int_param(qs, "fnid")
        fn_row = conn.execute(
            "SELECT NAME FROM FUNCTIONS WHERE ID = ?",
            (fnid,)).fetchone()
        if fn_row is None:
            self.send_error_json(404, "Function not found")
            return

        fn_name = fn_row["NAME"]

        callers = conn.execute(
            """SELECT f.ID, f.NAME FROM FCALLS fc
            JOIN FUNCTIONS f ON fc.SOURCEID = f.ID
            WHERE fc.DESTID = ?""",
            (fnid,)).fetchall()
        callees = conn.execute(
            """SELECT f.ID, f.NAME FROM FCALLS fc
            JOIN FUNCTIONS f ON fc.DESTID = f.ID
            WHERE fc.SOURCEID = ?""",
            (fnid,)).fetchall()

        dot_lines = [
            "digraph callgraph {",
            '\trankdir=LR;',
            '\tgraph [bgcolor=transparent];',
            '\tnode [shape=box, fontname="Helvetica", fontsize=10, style=filled, fillcolor="#4a4080", fontcolor="#ffffff"];',
            '\tedge [fontsize=9, color="#aaaaaa", arrowsize=0.8];',
            f'\t"{fn_name}" [fillcolor="#4a9eda", fontcolor=white, penwidth=2];',
        ]

        for c in callers:
            dot_lines.append(f'\t"{c["NAME"]}" [fillcolor="#4a4080", fontcolor=white];')
            dot_lines.append(f'\t"{c["NAME"]}" -> "{fn_name}";')
        for c in callees:
            dot_lines.append(f'\t"{c["NAME"]}" [fillcolor="#2d6b5e", fontcolor=white];')
            dot_lines.append(f'\t"{fn_name}" -> "{c["NAME"]}";')

        dot_lines.append("}")
        dot_source = "\n".join(dot_lines)

        try:
            result = subprocess.run(
                ["dot", "-Tsvg"],
                input=dot_source.encode("utf-8"),
                capture_output=True,
                timeout=10
            )
            if result.returncode != 0:
                self.send_error_json(500,
                    f"graphviz error: {result.stderr.decode()}")
                return
            svg = result.stdout.decode("utf-8")
        except FileNotFoundError:
            self.send_error_json(500,
                "graphviz not found. Install with: apt install graphviz")
            return
        except subprocess.TimeoutExpired:
            self.send_error_json(500, "graphviz timed out")
            return

        html = f"""<!doctype html>
    <html><head><meta charset="utf-8">
    <style>
    body {{ margin: 0; background: #1e1e1e; display: flex;
        align-items: center; justify-content: center; min-height: 100vh; }}
    svg {{ max-width: 100%; height: auto; }}
    </style>
    </head><body>{svg}</body></html>"""

        body = html.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def handle_rename_apply(self, conn, qs):
        """Handle /rename/apply requests.

        Performs byte-precise rename using token offsets from the DB.
        Modifies source files on disk. Only modifies writable files.
        Applies replacements from end to start within each file so
        earlier offsets are not shifted by the replacement.
        """
        eid = get_required_int_param(qs, "eid")
        newname = get_required_param(qs, "newname")
        id_row = conn.execute(
            "SELECT NAME FROM IDS WHERE EID = ?",
            (eid,)).fetchone()
        if id_row is None:
            self.send_error_json(404, "Identifier not found")
            return

        old_name = id_row["NAME"]
        old_len = len(old_name.encode("utf-8"))
        new_bytes = newname.encode("utf-8")

        tokens = conn.execute(
            """SELECT t.FID, f.NAME AS FILE, t.FOFFSET
               FROM TOKENS t
               JOIN FILES f ON t.FID = f.FID
               WHERE t.EID = ? AND f.RO = 0
               ORDER BY t.FID, t.FOFFSET DESC""",
            (eid,)).fetchall()

        by_file = {}
        for tok in tokens:
            fid = tok["FID"]
            if fid not in by_file:
                by_file[fid] = {"path": tok["FILE"], "offsets": []}
            by_file[fid]["offsets"].append(tok["FOFFSET"])

        modified_files = []
        for fid, info in by_file.items():
            fpath = info["path"]
            try:
                with open(fpath, "rb") as fh:
                    data = bytearray(fh.read())
                for offset in sorted(info["offsets"], reverse=True):
                    data[offset:offset + old_len] = new_bytes
                with open(fpath, "wb") as fh:
                    fh.write(data)
                modified_files.append(fpath)
            except OSError as e:
                self.send_error_json(
                    500, f"Failed to modify {fpath}: {e}")
                return

        self.send_json({
            "old_name": old_name,
            "new_name": newname,
            "modified_files": modified_files,
            "total_replacements": len(tokens),
        })

    def handle_filegraph_include(self, conn, qs):
        """File include graph via graphviz."""
        writable_only = get_bool_param(qs, "writable")
        if writable_only:
            rows = conn.execute("""
                SELECT DISTINCT f1.NAME AS SRC, f2.NAME AS DST
                FROM INCLUDERS i
                JOIN FILES f1 ON i.BASEFILEID = f1.FID
                JOIN FILES f2 ON i.INCLUDERID = f2.FID
                WHERE f1.RO = 0 AND f2.RO = 0
                AND f2.NAME NOT LIKE '%.cs'
            """).fetchall()
        else:
            rows = conn.execute("""
                SELECT DISTINCT f1.NAME AS SRC, f2.NAME AS DST
                FROM INCLUDERS i
                JOIN FILES f1 ON i.BASEFILEID = f1.FID
                JOIN FILES f2 ON i.INCLUDERID = f2.FID
                WHERE f2.NAME NOT LIKE '%.cs'
            """).fetchall()
        self._send_file_graph_svg(rows, "File include graph")

    def handle_filegraph_compile(self, conn, qs):
        """Compile-time dependency graph via graphviz."""
        writable_only = get_bool_param(qs, "writable")
        if writable_only:
            rows = conn.execute("""
                SELECT DISTINCT f1.NAME AS SRC, f2.NAME AS DST
                FROM DEFINERS d
                JOIN FILES f1 ON d.BASEFILEID = f1.FID
                JOIN FILES f2 ON d.DEFINERID = f2.FID
                WHERE f1.RO = 0
            """).fetchall()
        else:
            rows = conn.execute("""
                SELECT DISTINCT f1.NAME AS SRC, f2.NAME AS DST
                FROM DEFINERS d
                JOIN FILES f1 ON d.BASEFILEID = f1.FID
                JOIN FILES f2 ON d.DEFINERID = f2.FID
            """).fetchall()
        self._send_file_graph_svg(rows, "Compile-time dependency graph")

    def handle_filegraph_control(self, conn, qs):
        """Control dependency graph (through function calls) via graphviz."""
        writable_only = get_bool_param(qs, "writable")
        if writable_only:
            rows = conn.execute("""
                SELECT DISTINCT f1.NAME AS SRC, f2.NAME AS DST
                FROM FCALLS fc
                JOIN FUNCTIONS fn1 ON fc.SOURCEID = fn1.ID
                JOIN FUNCTIONS fn2 ON fc.DESTID = fn2.ID
                JOIN FILES f1 ON fn1.FID = f1.FID
                JOIN FILES f2 ON fn2.FID = f2.FID
                WHERE f1.FID != f2.FID AND f1.RO = 0 AND f2.RO = 0
            """).fetchall()
        else:
            rows = conn.execute("""
                SELECT DISTINCT f1.NAME AS SRC, f2.NAME AS DST
                FROM FCALLS fc
                JOIN FUNCTIONS fn1 ON fc.SOURCEID = fn1.ID
                JOIN FUNCTIONS fn2 ON fc.DESTID = fn2.ID
                JOIN FILES f1 ON fn1.FID = f1.FID
                JOIN FILES f2 ON fn2.FID = f2.FID
                WHERE f1.FID != f2.FID
            """).fetchall()
        self._send_file_graph_svg(rows, "Control dependency graph")

    def handle_filegraph_data(self, conn, qs):
        """Data dependency graph (through global variables) via graphviz."""
        writable_only = get_bool_param(qs, "writable")
        if writable_only:
            rows = conn.execute("""
                SELECT DISTINCT f1.NAME AS SRC, f2.NAME AS DST
                FROM TOKENS t1
                JOIN TOKENS t2 ON t1.EID = t2.EID AND t1.FID != t2.FID
                JOIN IDS i ON i.EID = t1.EID
                JOIN FILES f1 ON t1.FID = f1.FID
                JOIN FILES f2 ON t2.FID = f2.FID
                WHERE i.ORDINARY = 1 AND i.FUN = 0 AND i.READONLY = 0
                AND f1.RO = 0 AND f2.RO = 0
            """).fetchall()
        else:
            rows = conn.execute("""
                SELECT DISTINCT f1.NAME AS SRC, f2.NAME AS DST
                FROM TOKENS t1
                JOIN TOKENS t2 ON t1.EID = t2.EID AND t1.FID != t2.FID
                JOIN IDS i ON i.EID = t1.EID
                JOIN FILES f1 ON t1.FID = f1.FID
                JOIN FILES f2 ON t2.FID = f2.FID
                WHERE i.ORDINARY = 1 AND i.FUN = 0 AND i.READONLY = 0
            """).fetchall()
        self._send_file_graph_svg(rows, "Data dependency graph")

    def _send_file_graph_svg(self, rows, title):
        """Generate and send a graphviz SVG for file dependency edges."""
        if not rows:
            html = f"""<!doctype html><html><body style="font-family:sans-serif;padding:2em">
                <p>No dependencies found.</p></body></html>"""
            body = html.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        dot_lines = [
            "digraph filedeps {",
            '\trankdir=LR;',
            '\tgraph [bgcolor=transparent];',
            '\tnode [shape=box, fontname="Helvetica", fontsize=9];',
            '\tedge [fontsize=8, color="#aaaaaa", arrowsize=0.8];',
        ]
        seen = set()
        for row in rows:
            src = os.path.basename(row["SRC"])
            dst = os.path.basename(row["DST"])
            if src not in seen:
                seen.add(src)
                if src.endswith('.h'):
                    dot_lines.append(f'\t"{src}" [fillcolor="#3a6fa8", style=filled, fontcolor=white];')
                else:
                    dot_lines.append(f'\t"{src}" [fillcolor="#2d6b40", style=filled, fontcolor=white];')
            if dst not in seen:
                seen.add(dst)
                if dst.endswith('.h'):
                    dot_lines.append(f'\t"{dst}" [fillcolor="#3a6fa8", style=filled, fontcolor=white];')
                else:
                    dot_lines.append(f'\t"{dst}" [fillcolor="#2d6b40", style=filled, fontcolor=white];')
            dot_lines.append(f'\t"{src}" -> "{dst}";')
        dot_lines.append("}")
        dot_source = "\n".join(dot_lines)

        try:
            result = subprocess.run(
                ["dot", "-Tsvg"],
                input=dot_source.encode("utf-8"),
                capture_output=True,
                timeout=15
            )
            if result.returncode != 0:
                self.send_error_json(500, f"graphviz error: {result.stderr.decode()}")
                return
            svg = result.stdout.decode("utf-8")
        except FileNotFoundError:
            self.send_error_json(500, "graphviz not found")
            return
        except subprocess.TimeoutExpired:
            self.send_error_json(500, "graphviz timed out")
            return

        html = f"""<!doctype html>
    <html><head><meta charset="utf-8">
    <style>
    body {{ margin: 0; background: #1e1e1e; display: flex; flex-direction: column;
        align-items: center; padding: 1em; }}
    h2 {{ color: #ccc; font-family: Helvetica; font-size: 14px; margin-bottom: 1em; }}
    svg {{ max-width: 100%; height: auto; }}
    </style>
    </head><body>
    <h2>{title}</h2>
    {svg}
    </body></html>"""

        body = html.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)


def main():
    global _db_path

    parser = argparse.ArgumentParser(
        description="REST API server for querying a CScout SQLite database.")
    parser.add_argument("db",
                        help="Path to the CScout SQLite database file")
    parser.add_argument("-p", "--port", type=int, default=8081,
                        help="Port to listen on (default: 8081)")
    parser.add_argument("--monitor-stdin", action="store_true",
                        help="Monitor stdin and exit when it is closed (EOF)")
    parser.add_argument("--pid-file", type=str, default=None,
                        help="Path to write the server's PID file")
    args = parser.parse_args()

    _db_path = args.db

    try:
        conn = sqlite3.connect(_db_path)
        conn.execute("SELECT COUNT(*) FROM IDS")
        conn.close()
    except Exception as e:
        print(f"Error: cannot open database {_db_path}: {e}",
              file=sys.stderr)
        sys.exit(1)

    if args.monitor_stdin:
        def monitor_parent():
            try:
                sys.stdin.read()
            except Exception:
                pass
            os._exit(0)
        threading.Thread(target=monitor_parent, daemon=True).start()

    if args.pid_file:
        try:
            with open(args.pid_file, "w") as f:
                json.dump({"pid": os.getpid(), "port": args.port}, f)
        except Exception as e:
            print(f"Warning: could not write pid file: {e}", file=sys.stderr)

    server = HTTPServer(("127.0.0.1", args.port), Handler)
    print(f"CScout API ready at http://127.0.0.1:{args.port}",
          file=sys.stderr)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        if args.pid_file:
            try:
                os.remove(args.pid_file)
            except Exception:
                pass


if __name__ == "__main__":
    main()
