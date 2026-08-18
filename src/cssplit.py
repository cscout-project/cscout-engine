#!/usr/bin/env python3
"""
Split CScout processing file into multiple processing shards.
"""

import sys
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, TextIO
import argparse
import re
import signal

@dataclass(frozen=True)
class InputCoords:
    """Coordinates for file input"""
    # In input_files
    file_ordinal: int = -1
    # Within input_files[file_ordinal]
    offset: int = -1


@dataclass
class CUProps:
    """Properties for each compilarion unit"""
    path: str
    # Coordinates for each recorded project instance
    coords: List[InputCoords] = field(default_factory=list)
    # Projects to which the CU belongs
    projects: List[bool] = field(default_factory=list)
    # True if allocated to a shard
    allocated: bool = False

    def number_of_projects(self):
        """Return number of projects in which this CU belongs."""
        return sum(1 for v in self.projects if v)

    def add_project(self, project_id: int, input_file_ordinal: int, offset: int):
        """Add the CU to the specified offset in project."""
        if project_id >= len(self.projects):
            self.projects.extend([False] * (project_id + 1 - len(self.projects)))
            self.coords.extend([InputCoords()] * (project_id + 1 - len(self.coords)))
        self.projects[project_id] = True
        self.coords[project_id] = InputCoords(input_file_ordinal, offset)

    def in_project(self, project_id: int):
        """Return true if the CU belongs to the specified project."""
        return project_id < len(self.projects) and self.projects[project_id]

    def write(self, output_file: TextIO, project_id: int):
        """Write out the specified compilation unit for the given
        project."""
        coords = self.coords[project_id]
        input_file = input_files[coords.file_ordinal]
        input_file.seek(coords.offset, 0)
        for line in input_file:
            output_file.write(line)
            if line.startswith('#pragma echo "Done processing /'):
                return

project_name_to_id: dict[str, int] = {}
project_id_to_name: dict[int, str] = {}

project_id = -1

# Total number of compilation units
ncus = 0

cu_to_props: dict[str, CUProps] = {}

cu_list: list[CUProps] = []

# Input files from which the project can read
input_files: list[TextIO] = []

RE_PROJECT_START = re.compile(r'^#pragma\s+project\s+"([^"]+)"')
RE_CU_START = re.compile(r'^#pragma\s+echo\s+"Processing (/[^\\]+)\\n"')

def begin_shard(file_number):
    """Start a new output file."""
    outfile = Path(f"file-{file_number:04d}.cs")
    f = outfile.open("w")
    return f

def begin_project(file, name):
    """Start a new project."""
    file.write(f'#pragma echo "Processing project {name}\\n"\n')
    file.write(f'#pragma project "{name}"\n')
    file.write("#pragma block_enter\n")

def end_project(file, name):
    file.write("#pragma block_exit\n")
    file.write(f'#pragma echo "Done processing project {name}\\n\\n"\n')

"""
Algorithm outline
- Go through all specified .cs files keeping the CU offset and project
  for each CU
- Determine CUs per shard
- Go through all files in encountered order
- Allocate each encountered CU and all CUs with the same path to a shard
  until the shard fills up.
- For each shard, for each project, write out the corresponding CUs.
"""
def read_projects(input_file):

    global input_files
    global project_id
    global ncus

    input_file_ordinal = len(input_files)
    input_files.append(input_file)

    # Read properties for each CU
    while True:
        offset = input_file.tell()
        line = input_file.readline()
        if not line:
            break
        line = line.rstrip()

        if matched := RE_PROJECT_START.search(line):
            project_id += 1
            project_name = matched.group(1)
            project_name_to_id[project_name] = project_id
            project_id_to_name[project_id] = project_name
            print(f"Reading project {project_id} [{project_name}]")

        elif matched := RE_CU_START.search(line):
            cu_path = matched.group(1)
            ncus += 1
            if cu_path in cu_to_props:
                props = cu_to_props[cu_path]
            else:
                props = CUProps(cu_path)
                cu_to_props[cu_path] = props
            props.add_project(project_id, input_file_ordinal, offset)

            cu_list.append(props)


def allocate(nshards: int):
    global project_id
    global ncus

    nprojects = project_id + 1
    max_shard_cus = ncus / nshards

    # Allocate CUs to shards
    print("Allocating compilation units")
    shard: list[CUProps] = []
    all_shards: list[list[CUProps]] = [shard]
    shard_cus: int = 0
    for cu in cu_list:
        if cu.allocated:
            continue
        shard.append(cu)
        shard_cus += cu.number_of_projects()
        cu.allocated = True
        if shard_cus >= max_shard_cus:
            shard = []
            all_shards.append(shard)
            shard_cus = 0

    # Write out the projects and the CUs in each shard
    shard_number = 0
    for shard in all_shards:
        print(f"Writing out shard {shard_number}")
        output_file = begin_shard(shard_number)
        for project_id in range(0, nprojects):
            project_name = project_id_to_name[project_id]
            print(f"Writing out project {project_id} [{project_name}]")
            begin_project(output_file, project_name)
            for cu in shard:
                if cu.in_project(project_id):
                    cu.write(output_file, project_id)
            end_project(output_file, project_name)
        shard_number += 1

def main():
    """Program entry point"""
    # Ignore unsightly broken pipe exceptions
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)

    parser = argparse.ArgumentParser(
        description='CScout file sharder')
    parser.add_argument('-s', '--shards',
                        help='Number of shards to create',
                        type=int,
                        required=True)
    parser.add_argument('file',
                        help='Files to process',
                        nargs='+',
                        type=str)
    args = parser.parse_args()
    for file_name in args.file:
        input_file = open(file_name)
        read_projects(input_file)
    allocate(args.shards)

if __name__ == "__main__":
    main()
