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
# cscoco.py -- generate a CScout workspace file from
# a compile_commands.json compilation database.
#

import argparse
import json
import os
import sys


def parse_flags(entry):
    """
    Extract preprocessor defines and include paths from a
    compile_commands.json entry.

    An entry may carry its compiler invocation either as a single
    'command' string or as an 'arguments' list.  The arguments form
    is preferred because it avoids shell-quoting ambiguity; the
    command form is used as a fallback.

    Returns (defines, includes) where each is a list of strings:
      defines  -- bare macro names or NAME=VALUE pairs, without the -D
      includes -- directory paths, without the -I
    """
    # Prefer the pre-split arguments array; fall back to shell split.
    if 'arguments' in entry:
        parts = entry['arguments']
    else:
        parts = entry['command'].split()

    defines = []
    undefines = []
    includes = []
    i = 0
    while i < len(parts):
        part = parts[i]
        if part == '-D':
            # -D NAME (space-separated form)
            if i + 1 < len(parts):
                defines.append(parts[i + 1])
                i += 2
            else:
                i += 1
        elif part.startswith('-D'):
            # -DNAME or -DNAME=VALUE (attached form)
            defines.append(part[2:])
            i += 1
        elif part == '-U':
            # -U NAME
            if i + 1 < len(parts):
                undefines.append(parts[i + 1])
                i += 2
            else:
                i += 1
        elif part.startswith('-U'):
            undefines.append(part[2:])
            i += 1
        elif part in ('-I', '-isystem', '-iquote', '-idirafter'):
            # -I path (space-separated form)
            if i + 1 < len(parts):
                includes.append(parts[i + 1])
                i += 2
            else:
                i += 1
        elif part.startswith('-I'):
            includes.append(part[2:])
            i += 1
        elif part.startswith('-isystem'):
            includes.append(part[8:])
            i += 1
        elif part.startswith('-iquote'):
            includes.append(part[7:])
            i += 1
        elif part.startswith('-idirafter'):
            includes.append(part[10:])
            i += 1
        else:
            i += 1

    return defines, undefines, includes

def generate_cs(compile_commands_file, project_name):
    """
    Read compile_commands.json from a file object and print the corresponding
    CScout workspace file to standard output.

    Each C translation unit in the compilation database becomes a
    block in the .cs file that sets up the preprocessor environment
    (defines and include paths) exactly as the original compiler
    invocation did, then processes the source file.
    """
    
    # Find the CScout installation / config directory
    if os.path.isdir('.cscout'):
        instdir = '.cscout'
    elif 'CSCOUT_HOME' in os.environ:
        instdir = os.environ['CSCOUT_HOME']
    elif 'HOME' in os.environ and os.path.isdir(os.path.join(os.environ['HOME'], '.cscout')):
        instdir = os.path.join(os.environ['HOME'], '.cscout')
    elif os.path.isdir('test/csmake/include/cscout'):
        instdir = 'test/csmake/include/cscout'
    else:
        instdir = 'INSTALL_INCLUDE'

    predefs = os.path.join(instdir, 'csmake-pre-defs.h')
    postdefs = os.path.join(instdir, 'csmake-post-defs.h')
    hostdefs = os.path.join(instdir, 'host-defs.h')
    hostincs = os.path.join(instdir, 'host-incs.h')

    if not os.path.exists(predefs):
        print(
            'Error: workaround headers not found.\n'
            'Please refer to the CScout installation instructions.',
            file=sys.stderr)
        sys.exit(1)

    if not os.path.exists(hostdefs) or not os.path.exists(hostincs):
        print(
            'Error: host-defs.h or host-incs.h not found.\n'
            'Please refer to the CScout installation instructions.',
            file=sys.stderr)
        sys.exit(1)

    entries = json.load(compile_commands_file)

    # Skip non-C files; CScout analyses C, not C++.
    c_entries = [e for e in entries if e['file'].endswith('.c')]

    if not c_entries:
        print('Warning: no .c files found in compilation database.',
              file=sys.stderr)

    print(f'/*\n'
          f' * Generated by cscoco\n'
          f' * Source: {compile_commands_file.name}\n'
          f' */\n')
    print(f'#pragma project "{project_name}"')
    print('#pragma block_enter\n')

    for entry in c_entries:
        filepath = entry['file']
        directory = entry['directory']
        defines, undefines, includes = parse_flags(entry)

        print(f'#pragma echo "Processing {os.path.basename(filepath)}\\n"')
        print('#pragma block_enter')
        print('#pragma clear_defines')
        print('#pragma clear_include')
        print(f'#pragma pushd "{directory}"')

        # Pre-defs supply CScout's mock standard headers.
        # host-defs supplies the actual compiler's built-in macro
        # definitions (e.g. __GNUC__) since csmake-pre-defs.h
        # deliberately omits these (it targets the csmake workflow,
        # where a real compiler provides them separately).
        # host-incs supplies the actual system include search paths.
        print(f'#include "{predefs}"')
        print(f'#include "{hostdefs}"')
        print(f'#include "{hostincs}"')

        for d in defines:
            if '=' in d:
                name, val = d.split('=', 1)
                print(f'#define {name} {val}')
            else:
                print(f'#define {d}')

        for u in undefines:
            print(f'#undef {u}')

        for inc in includes:
            print(f'#pragma includepath "{inc}"')

        # Post-defs close any open constructs from pre-defs.
        print(f'#include "{postdefs}"')
        print(f'#pragma process "{filepath}"')
        print('#pragma popd')
        print('#pragma block_exit\n')

    print('#pragma block_exit')


def main():
    parser = argparse.ArgumentParser(
        description='Generate a CScout workspace file from a compile_commands.json compilation database.')
    parser.add_argument('compile_commands',
                        nargs='?',
                        type=argparse.FileType('r', encoding='utf-8'),
                        default=sys.stdin,
                        help='Path to the compile_commands.json file (defaults to stdin)')
    parser.add_argument('-n', '--name',
                        dest='project_name',
                        help='Name of the project (optional; defaults to the name of the directory above the build directory or "project" if reading from stdin)',
                        default=None)
    args = parser.parse_args()

    project = args.project_name
    if project is None:
        if args.compile_commands.name != '<stdin>':
            build_dir = os.path.dirname(os.path.abspath(args.compile_commands.name))
            project = os.path.basename(os.path.dirname(build_dir))
        else:
            project = 'project'

    generate_cs(args.compile_commands, project)


if __name__ == '__main__':
    main()
