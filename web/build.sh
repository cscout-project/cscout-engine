#!/bin/sh
#
# Create the CScout top web pages
#

# For each file appearing in top.html
for file in `gawk -F\" '$4 == "menu" && $6 != ".." {print $6}' top.html`
do
	base=`basename $file .html`
	(
	# Highlight menu marker
	cat top.html |
	sed '\|class="menu" href="'$file'"|s/menu/selmenu/
		s/class="menu" href="doc\//class="menu" href="/'
	if expr $file : doc/ >/dev/null
	then
		source=../doc/$base.xml
		sed -n '/<notes>/,/<\/notes>/{;/notes>/d;s/fmtcode.*>/pre>/;p;}' $source
	else
		source=$file
		cat $file
	fi
        sed "s,XDATE,`git log -1 --format='%ad (%h)' --date=short -- "$source"`," bottom.html
	) >build/$base.html
	xml val -d d:/pub/schema/xhtml1-transitional.dtd build/$base.html
done
