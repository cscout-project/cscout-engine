#
# (C) Copyright 2001-2026 Diomidis Spinellis
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

PREFIX?=/usr/local
MANDIR?=$(PREFIX)/share/man/man1

# By default a production build is made.
# For a debug build run make as:
# make DEBUG=1

WEBHOME=$(UH)/dds/pubs/web/home/cscout/

.PHONY: test tags

ADDLIBS+=../swill/libswill.a

# Common object files
OBJBASE=eclass.o fchar.o filedetails.o fileid.o pdtoken.o pltoken.o debug.o \
  ptoken.o tchar.o token.o tokid.o tokname.o eval.o ctoken.o macro.o \
  parse.o type.o stab.o attr.o metrics.o version.o dbtoken.o \
  error.o fdep.o fcall.o call.o idquery.o query.o funquery.o \
  logo.o workdb.o obfuscate.o sql.o md5.o os.o pager.o \
  option.o filequery.o mcall.o filemetrics.o funmetrics.o ctconst.o \
  dirbrowse.o html.o fileutils.o util.o options.o engine.o gdisplay.o globobj.o ctag.o timer.o \
  static_init.o initializer.o

# monitor.o

ifdef PICO_QL
OBJBASE += pico_ql_search.o pico_ql_vt.o pico_ql_interface.o \
 pico_ql_search_helper.o pico_ql_test.o sqlite3.o \
 pico_ql_logo.o pico_ql_error_page.o
endif

# Determine architecture and OS
OS=$(shell uname -s)

OBJ=$(addprefix build/, $(OBJBASE))

.SUFFIXES:.java .dot .ps .png .pic .eps .png .svg

# C/C++ files that are under version control
# (Not auto-generated, apart from logo.cpp)
CFILES=md5.cpp attr.cpp call.cpp cscout.cpp ctag.cpp ctconst.cpp \
  ctoken.cpp debug.cpp dirbrowse.cpp eclass.cpp error.cpp fcall.cpp \
  fchar.cpp fdep.cpp filedetails.cpp fileid.cpp filemetrics.cpp filequery.cpp \
  fileutils.cpp \
  funmetrics.cpp funquery.cpp gdisplay.cpp globobj.cpp html.cpp idquery.cpp \
  logo.cpp macro.cpp mcall.cpp metrics.cpp obfuscate.cpp option.cpp os.cpp \
  pager.cpp pdtoken.cpp pltoken.cpp ptoken.cpp query.cpp simple_cpp.cpp \
  sql.cpp stab.cpp tchar.cpp timer.cpp token.cpp tokid.cpp \
  tokmap.cpp type.cpp workdb.cpp static_init.cpp dbtoken.cpp

HEADERS=attr.h call.h compiledre.h cpp.h ctag.h ctconst.h ctoken.h \
  debug.h defs.h dirbrowse.h eclass.h error.h eval.h fcall.h fchar.h fdep.h \
  fifstream.h filedetails.h fileid.h filemetrics.h filequery.h fileutils.h \
  funmetrics.h \
  funquery.h gdisplay.h globobj.h html.h id.h idquery.h incs.h logo.h \
  macro.h mcall.h md5.h metrics.h mquery.h mscdefs.h mscincs.h obfuscate.h \
  option.h os.h pager.h pdtoken.h pltoken.h ptoken.h query.h sql.h stab.h \
  swill.h tchar.h timer.h token.h tokid.h tokmap.h type.h type2.h version.h \
  wdefs.h wincs.h workdb.h ytoken.h macro_arg_processor.h dbtoken.h \
  debug_out.h

OTHERSRC=style.css csmake.pl cswc.pl tokname.pl runtest.sh eval.y parse.y \
  Makefile

# Auto-generated C files
AUTOCFILES=css.cpp eval.cpp logo.cpp parse.cpp tokname.cpp version.cpp

AUTOHEADERS=parse.tab.h

YACC=../btyacc/btyacc
#YACC=yacc

# Compile with CPPFLAGS=-DNO_DP to remove debugpoint support and thereby
# improve processing throughput.
CPPFLAGS+=-pipe -Wall -Wextra -Wpedantic -I. -DPREFIX='"$(PREFIX)"'
CXXFLAGS+=-std=c++17
ifdef DEBUG
# Debug build
# To get yacc debugging info set YYDEBUG environment variable to 1
# To get stack traces for STL problems use gdb and break _Error_formatter::_M_at
CPPFLAGS+=-D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC -DDEBUG
CFLAGS+=-g
CXXFLAGS+=-g
# -D_GLIBCXX_CONCEPT_CHECKS
else
	ifdef PROFILE
		CFLAGS+=-O0 -g -pg
		CXXFLAGS+=-O0 -g -pg
	else
		CPPFLAGS+=-DPRODUCTION
		CFLAGS+=-O3
		CXXFLAGS+=-O3
	endif
endif

# Test coverage analysis
# Buld with make DEBUG=1 TCOV=1
# Test with sh runtest.sh
# Then run gcov token.cpp -o i386
ifdef TCOV
CFLAGS+=-ftest-coverage -fprofile-arcs
CXXFLAGS+=-ftest-coverage -fprofile-arcs
endif

# Local development flag options
# Displays information on licensing protocol
# Will always call the licensing server

# Moving away from static linking
#CPPFLAGS += -static

ifeq ($(OS),SunOS)
ADDLIBS += -lsocket -lnsl
#OBJ += dlstubs.o
endif

ifeq ($(OS),Windows_NT)
CPPFLAGS += -I.
endif

ifeq (build,sparc)
CFLAGS+=-mcpu=v8
CXXFLAGS+=-mcpu=v8
endif

ifdef PICO_QL
CPPFLAGS += -DPICO_QL -DPICO_QL_SINGLE_THREADED -I../sqlite-amalgamation

ifeq ($(OS),Windows_NT)
CPPFLAGS += -I/vol/boost_1_44_0
else
CPPFLAGS += -I../boost_1_44_0
LDFLAGS += -ldl -lpthread
endif

endif

CPPFLAGS+=$(EXTRA_CPPFLAGS)

# Pattern rules for C++ files
build/%.o: %.cpp
	$(CXX) -c $(CXXFLAGS) $(CPPFLAGS) -o $@ $<

# Dependency generation rules
# See https://www.gnu.org/software/make/manual/html_node/Automatic-Prerequisites.html
dep/%.d: %.cpp
	@set -e; rm -f $@; \
	mkdir -p dep ; \
	$(CXX) -MM -MG $(CPPFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,build/\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

# Serialize execution order to ensure the dependencies are built before
# compiling the object files in make -j invocations
all:
	$(MAKE) depend
	$(MAKE) build/cscout

depend:

build/cscout: build/cscout.o $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o build/cscout  $(OBJ) build/cscout.o $(ADDLIBS)

# CCmalloc version
build/cscout-cc: build/cscout.o  $(OBJ)
	ccmalloc $(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o build/cscout-cc  $(OBJ) build/cscout.o -L${HOME}/lib/build -lswill

# The only way to avoid having the rule being executed multiple times
# under make -j in rules with multiple targets is to define a single
# pattern rule rather than multiple explicit rules.
# See "Rules With Multiple Outputs in GNU Make"
# http://www.cmcrossroads.com/article/rules-multiple-outputs-gnu-make?page=0%2C2
%.cpp %.tab.h: %.y ytoken.h
	@test $* = parse && echo Expect 3 shift/reduce conflicts || true
	@echo Expect 18 shift/reduce conflicts, 84 reduce/reduce conflicts
	$(YACC) -d -b $* -p $*_ $*.y
	mv -f $*.tab.c $*.cpp

tokname.cpp: parse.tab.h tokname.pl
	perl tokname.pl parse.tab.h tokname.cpp

build/html.o: html.cpp css.cpp

css.cpp: style.css
	tr -d \\r <$? | sed 's/\\/\\\\/g;s/"/\\"/g;s/^/"/;s/$$/\\n"/' >$@

# Create error message documentation
mkerr:
	perl mkerr.pl >../doc/error.xml

# This ensures that the version.cpp is regenerated iff its SHA doesn't
# match the current one.
ifneq ($(shell sed -n '/Version::revision/s/.* = "\(.*\)";/\1/p' version.cpp 2>/dev/null), $(shell git log -n 1 --format='%H' || sed -n '/Version::revision/s/.* = "\(.*\)";/\1/p' version.cpp))
version.cpp:
	sh make-version.sh >$@

.PHONY: version.cpp
endif


test:
	./runtest.sh $(TEST_FLAGS)
	cd test/csmake && ./runtest.sh
	make -C csmerge test
	python3 test_csapi.py

tags:
	ctags *.cpp *.h *.y

# Used for regenerating the logo
logo.cpp: logo.png
	echo '#include <stdio.h>' >logo.cpp
	echo '#include "logo.h"' >>logo.cpp
	echo 'const unsigned char Logo::logo_bytes[] = {' >>logo.cpp
	od -vb logo.png | tr -d "\r" | sed 's/^.......//;s/^ /0/;s/ /,0/g;s/$$/,/;s/^,$$//' >>logo.cpp
	echo '};' >>logo.cpp
	echo 'int Logo::len = sizeof(Logo::logo_bytes);' >>logo.cpp

wc:
	wc -l $(CFILES) $(HEADERS) $(OTHERSRC)

clean:
	rm -f build/* parse.tab.h parse.cpp tokname.cpp eval.cpp dep/*

.java.dot:
	javadoc -docletpath /dds/src/research/umlgraph/lib/UmlGraph.jar -doclet org.umlgraph.doclet.UmlGraph -private $<
	mv graph.dot $@

.dot.png:
	dot -Tpng -o$@ $<

classdiag.java: $(HEADERS) $(AUTOHEADERS)
	grep -h ":.*public" *.h | \
	egrep -v '(struct|binary_func)' | \
	sed 's/class //;s/://;s/public //;s/{//;s/  */ /g' | \
	awk '{print "class " $$1 " extends " $$2 " {}"}' >$@

classdiag.png: classdiag.java

dbdump:
	for db in mysql hsqldb postgres sqlite;\
	do \
		(cd ../example ; \
		../refactor/i386/cscout -s $$db awk.cs | gzip -c > $(WEBHOME)/awk-$$db.sql.gz) ;\
	done

obfuscation:
	( cd ../example.obf ; \
	sh run.sh ; \
	tar cf - awk | gzip -c > $(WEBHOME)/awk-obf.tar.gz ; \
	zip -r $(WEBHOME)/awk-obf.zip awk ; \
	)

install: build/cscout
	install -Ds $< "$(PREFIX)/bin/cscout"
	./dest-install.sh "$(PREFIX)"
	install -d $(MANDIR)
	for i in ../man/*.1 ; do install -m 644 $$i $(MANDIR) ; done

uninstall:
	rm -f "$(PREFIX)/bin/csapi"
	rm -f "$(PREFIX)/bin/cscc"
	rm -f "$(PREFIX)/bin/cscout"
	rm -f "$(PREFIX)/bin/csmake"
	rm -f "$(PREFIX)/bin/cswc"
	rm -f "$(PREFIX)/bin/cscut"
	rm -f "$(PREFIX)/bin/cssplit"
	rm -f "$(PREFIX)/bin/cscoco"
	rm -f "$(PREFIX)/bin/csreconst"
	rm -rf "$(PREFIX)/include/cscout"
	rm -rf "$(PREFIX)/lib/csmerge"
	for i in ../man/*.1 ; do rm -f $(MANDIR)/$$(basename $$i) ; done

example: build/cscout
	cd ../example && ../src/build/cscout awk.cs

# Include dependencies
# See https://www.gnu.org/software/make/manual/html_node/Automatic-Prerequisites.html
include $(patsubst %.cpp, dep/%.d, $(CFILES) $(AUTOCFILES))

ifdef PICO_QL
pico_ql_search.cpp: pico_ql_generator.rb cscout-data.sql
	perl picoschema.pl cscout-data.sql >cscout-data.auto
	ruby pico_ql_generator.rb cscout-data.auto

pico_ql_search.o: pico_ql_search.cpp pico_ql_search.h pico_ql_interface.h pico_ql_search_helper.h

pico_ql_vt.o: pico_ql_vt.c pico_ql_vt.h pico_ql_search.h
pico_ql_interface.o: pico_ql_interface.c pico_ql_interface.h pico_ql_test.h
pico_ql_search_helper.o: pico_ql_search_helper.cpp pico_ql_search_helper.h pico_ql_search.h
pico_ql_test.o: pico_ql_test.c pico_ql_test.h
pico_ql_logo.o: pico_ql_logo.c pico_ql_swill_access_func.h
pico_ql_error_page.o: pico_ql_error_page.c pico_ql_swill_access_func.h


sqlite3.o: sqlite3.o

endif
