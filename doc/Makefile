DESTDIR=/dds/pubs/web/home/cscout
NOTETOOLS=/dds/pubs/courses/tools
MANDOC=mancscout.xml mancswc.xml mancsmake.xml mancscc.xml mancscut.xml \
       mancssplit.xml mancsmerge.xml mancsreconst.xml mancscoco.xml mancsapi.xml

.SUFFIXES:.java .dot .ps .gif .pic .eps .png .1

.java.dot:
	javadoc -docletpath ../lib/UmlGraph.jar -doclet gr.spinellis.umlgraph.doclet.UmlGraph -private $<
	mv graph.dot $@

.dot.eps:
	dot -Teps -o$@ $<

.dot.gif:
	dot -Tgif $< -o$@

.pic.eps:
	perl -p -e 'BEGIN {binmode(STDOUT);} s/\r//' $< | $(PIC2PLOT) -Teps >$@

man%.xml: ../man/%.1
	echo '<?xml version="1.0" ?>' > $@
	echo '<notes>' >> $@
	groff -mhtml -Thtml -man <$< | sed -e '1,/<body>/d;/<\/body>/,$$d;s/&minus;/-/g;/<h1/d;s/&bull;/\&#8226;/g;s/&quot;/"/g' >>$@
	echo '</notes>' >> $@

all: $(MANDOC) dbschema.xml qeg.xml
	rm -f style.css
	cp -u ../src/style.css .
	chmod 666 style.css
	cat local.css >>style.css
	chmod 444 style.css
	sh /cygdrive/c$(NOTETOOLS)/coursebib.sh bib
	echo "The CScout Refactoring Browser" | perl $(NOTETOOLS)/thread.pl -h html -r -p ../index.html index ${DESTDIR}/doc
	cp -u simul.html ${UH}/${DESTDIR}/doc
	cp -u logo.png ${UH}/${DESTDIR}
	cp -u stock*.png emacs.css ${UH}/${DESTDIR}/doc

qeg.xml: mksqleg.sh
	./mksqleg.sh >$@

dbschema.xml: ../src/build/cscout
	echo '<?xml version="1.0" ?>' >$@
	echo '<notes>' >>$@
	echo '<fmtcode ext="SQL">' >>$@
	../src/build/cscout -S postgres >>$@
	echo '</fmtcode>' >>$@
	echo '</notes>' >>$@

clean:
	rm ${UH}/${DESTDIR}/*
