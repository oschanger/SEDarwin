# $FreeBSD: src/share/colldef/Makefile,v 1.53 2002/12/30 09:09:57 ache Exp $
#

NOMAN=YES
CLEANFILES+= ${LOCALES:S/$/.out/g}

LOCALES=	bg_BG.CP1251 \
		ca_ES.ISO8859-1 \
		ca_ES.ISO8859-15 \
		cs_CZ.ISO8859-2 \
		de_DE.ISO8859-1 \
		de_DE.ISO8859-15 \
		el_GR.ISO8859-7 \
		es_ES.ISO8859-1 \
		es_ES.ISO8859-15 \
		et_EE.ISO8859-15 \
		hi_IN.ISCII-DEV \
		is_IS.ISO8859-1 \
		is_IS.ISO8859-15 \
		la_LN.ISO8859-1 \
		la_LN.ISO8859-15 \
		la_LN.ISO8859-2 \
		la_LN.ISO8859-4 \
		la_LN.US-ASCII \
		lt_LT.ISO8859-4 \
		lt_LT.ISO8859-13 \
		pl_PL.ISO8859-2 \
		ru_RU.CP866 \
		ru_RU.ISO8859-5 \
		ru_RU.KOI8-R \
		sr_YU.ISO8859-5 \
		sv_SE.ISO8859-1 \
		sv_SE.ISO8859-15 \
		uk_UA.ISO8859-5 \
		uk_UA.KOI8-U

LOCALEDIR=      ${DESTDIR}/usr/share/locale

ASCIILINKS=	en_AU.US-ASCII en_CA.US-ASCII en_GB.US-ASCII en_NZ.US-ASCII \
		en_US.US-ASCII ko_KR.eucKR ja_JP.eucJP ja_JP.SJIS \
		tr_TR.ISO8859-9 zh_CN.eucCN
DELINKS=	de_AT de_CH
ZHLINKS=	zh_TW.Big5

LATIN1LINKS=	af_ZA da_DK en_AU en_CA en_GB en_NZ en_US fi_FI \
		fr_BE fr_CA fr_CH fr_FR it_CH it_IT nl_BE nl_NL no_NO \
		pt_PT

LATIN2LINKS=	hr_HR hu_HU ro_RO sl_SI sr_YU

.SUFFIXES: .src .out

.src.out:
	colldef -I ${.CURDIR} -o ${.TARGET} ${.IMPSRC}

all: ${LOCALES:S/$/.out/g}

bg_BG.CP1251.out:	map.CP1251
ca_ES.ISO8859-15.out:   map.ISO8859-15
ca_ES.ISO8859-1.out:    map.ISO8859-1
cs_CZ.ISO8859-2.out:    map.ISO8859-2
de_DE.ISO8859-15.out:   map.ISO8859-15
de_DE.ISO8859-1.out:    map.ISO8859-1
el_GR.ISO8859-7.out:    map.ISO8859-7
es_ES.ISO8859-15.out:   map.ISO8859-15
es_ES.ISO8859-1.out:    map.ISO8859-1
et_EE.ISO8859-15.out:   map.ISO8859-15
hi_IN.ISCII-DEV.out:	map.ISCII-DEV
is_IS.ISO8859-15.out:   map.ISO8859-15
is_IS.ISO8859-1.out:    map.ISO8859-1
la_LN.ISO8859-15.out:   map.ISO8859-15
la_LN.ISO8859-1.out:    map.ISO8859-1
la_LN.ISO8859-2.out:    map.ISO8859-2
la_LN.ISO8859-4.out:    map.ISO8859-4
lt_LT.ISO8859-4.out:    map.ISO8859-4
lt_LT.ISO8859-13.out:   map.ISO8859-13
pl_PL.ISO8859-2.out:    map.ISO8859-2
ru_RU.CP866.out:        map.CP866
ru_RU.ISO8859-5.out:    map.ISO8859-5
ru_RU.KOI8-R.out:       map.KOI8-R
sr_YU.ISO8859-5.out:    map.ISO8859-5
sv_SE.ISO8859-15.out:   map.ISO8859-15
sv_SE.ISO8859-1.out:    map.ISO8859-1
uk_UA.ISO8859-5.out:	map.ISO8859-5
uk_UA.KOI8-U.out:       map.KOI8-U

afterinstall:
.for locale in ${LOCALES}
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
		${LOCALEDIR}/${locale}
	${INSTALL} -m 644 -o ${BINOWN} -g ${BINGRP} \
		${locale}.out ${LOCALEDIR}/${locale}/LC_COLLATE
.endfor
.for link in ${ASCIILINKS}
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
	       ${LOCALEDIR}/${link}
	ln -fs ../la_LN.US-ASCII/LC_COLLATE \
	       ${LOCALEDIR}/${link}/LC_COLLATE
.endfor
.for link in ${ZHLINKS}
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
	       ${LOCALEDIR}/${link}
	ln -fs ../is_IS.ISO8859-1/LC_COLLATE \
	       ${LOCALEDIR}/${link}/LC_COLLATE
.endfor
.for link in ${LATIN1LINKS}
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
	       ${LOCALEDIR}/${link}.ISO8859-1
	ln -fs ../la_LN.ISO8859-1/LC_COLLATE \
	       ${LOCALEDIR}/${link}.ISO8859-1/LC_COLLATE
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
	       ${LOCALEDIR}/${link}.ISO8859-15
	ln -fs ../la_LN.ISO8859-15/LC_COLLATE \
	       ${LOCALEDIR}/${link}.ISO8859-15/LC_COLLATE
.endfor
.for link in ${LATIN2LINKS}
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
	       ${LOCALEDIR}/${link}.ISO8859-2
	ln -fs ../la_LN.ISO8859-2/LC_COLLATE \
	       ${LOCALEDIR}/${link}.ISO8859-2/LC_COLLATE
.endfor
.for link in ${DELINKS}
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
	       ${LOCALEDIR}/${link}.ISO8859-1
	ln -fs ../de_DE.ISO8859-1/LC_COLLATE \
	       ${LOCALEDIR}/${link}.ISO8859-1/LC_COLLATE
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
	       ${LOCALEDIR}/${link}.ISO8859-15
	ln -fs ../de_DE.ISO8859-15/LC_COLLATE \
	       ${LOCALEDIR}/${link}.ISO8859-15/LC_COLLATE
.endfor
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
	       ${LOCALEDIR}/sk_SK.ISO8859-2
	ln -fs ../cs_CZ.ISO8859-2/LC_COLLATE \
	       ${LOCALEDIR}/sk_SK.ISO8859-2/LC_COLLATE
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
		${LOCALEDIR}/pt_BR.ISO8859-1
	ln -fs ../pt_PT.ISO8859-1/LC_COLLATE \
		${LOCALEDIR}/pt_BR.ISO8859-1/LC_COLLATE
	rm -f *.out

.include <bsd.prog.mk>
