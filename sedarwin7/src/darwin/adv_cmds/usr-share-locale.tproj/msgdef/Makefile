# $FreeBSD: src/share/msgdef/Makefile,v 1.44 2002/12/30 09:09:58 ache Exp $

NOMAN=YES
CLEANFILES+= ${LOCALES:S/$/.out/g}

LOCALES=	af_ZA.ISO8859-1 \
		bg_BG.CP1251 \
		ca_ES.ISO8859-1 \
		cs_CZ.ISO8859-2 \
		da_DK.ISO8859-1 \
		de_DE.ISO8859-1 \
		el_GR.ISO8859-7 \
		en_CA.ISO8859-1 \
		en_GB.ISO8859-1 \
		en_US.ISO8859-1 \
		es_ES.ISO8859-1 \
		et_EE.ISO8859-15 \
		fi_FI.ISO8859-1 \
		fr_BE.ISO8859-1 \
		fr_CH.ISO8859-1 \
		fr_FR.ISO8859-1 \
		hi_IN.ISCII-DEV \
		hr_HR.ISO8859-2 \
		hu_HU.ISO8859-2 \
		is_IS.ISO8859-1 \
		it_IT.ISO8859-1 \
		lt_LT.ISO8859-13 \
		nl_NL.ISO8859-1 \
		no_NO.ISO8859-1 \
		pl_PL.ISO8859-2 \
		pt_PT.ISO8859-1 \
		ro_RO.ISO8859-2 \
		ru_RU.CP866 \
		ru_RU.ISO8859-5 \
		ru_RU.KOI8-R \
		sl_SI.ISO8859-2 \
		sr_YU.ISO8859-2 \
		sr_YU.ISO8859-5 \
		sv_SE.ISO8859-1 \
		tr_TR.ISO8859-9 \
		uk_UA.ISO8859-5 \
		uk_UA.KOI8-U \
		ko_KR.eucKR \
		ja_JP.eucJP \
		ja_JP.SJIS \
		zh_TW.Big5 \
		zh_CN.eucCN

LOCALEDIR=	${DESTDIR}/usr/share/locale

ASCIILINKS=	en_CA en_GB en_US
LATIN15LINKS=   af_ZA ca_ES da_DK de_DE en_CA en_GB en_US es_ES fi_FI fr_BE \
		fr_CH fr_FR is_IS it_IT nl_NL no_NO pt_PT sv_SE

GB_LINKS=	en_AU en_NZ
FR_LINKS=	fr_CA
DE_LINKS=	de_AT de_CH
CZ_LINKS=	sk_SK
IT_LINKS=	it_CH
NL_LINKS=	nl_BE
PT_LINKS=	pt_BR

.SUFFIXES: .src .out

.src.out:
	grep -v '^#' < ${.IMPSRC} > ${.TARGET}

all: ${LOCALES:S/$/.out/g}

afterinstall:
.for lang in ${LOCALES}
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
			   ${LOCALEDIR}/${lang}/LC_MESSAGES
	${INSTALL} -m 644 -o ${BINOWN} -g ${BINGRP} ${lang}.out \
			   ${LOCALEDIR}/${lang}/LC_MESSAGES/LC_MESSAGES
.endfor
.for link in ${LATIN15LINKS}
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
	       ${LOCALEDIR}/${link}.ISO8859-15/LC_MESSAGES
	ln -sf ../../${link}.ISO8859-1/LC_MESSAGES/LC_MESSAGES \
	       ${LOCALEDIR}/${link}.ISO8859-15/LC_MESSAGES/LC_MESSAGES
.endfor
.for link in ${GB_LINKS}
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
		${LOCALEDIR}/${link}.ISO8859-1/LC_MESSAGES
	ln -sf ../../en_GB.ISO8859-1/LC_MESSAGES/LC_MESSAGES \
		${LOCALEDIR}/${link}.ISO8859-1/LC_MESSAGES/LC_MESSAGES
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
		${LOCALEDIR}/${link}.ISO8859-15/LC_MESSAGES
	ln -sf ../../en_GB.ISO8859-1/LC_MESSAGES/LC_MESSAGES \
		${LOCALEDIR}/${link}.ISO8859-15/LC_MESSAGES/LC_MESSAGES
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
		${LOCALEDIR}/${link}.US-ASCII/LC_MESSAGES
	ln -sf ../../en_GB.ISO8859-1/LC_MESSAGES/LC_MESSAGES \
		${LOCALEDIR}/${link}.US-ASCII/LC_MESSAGES/LC_MESSAGES
.endfor
.for link in ${FR_LINKS}
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
		${LOCALEDIR}/${link}.ISO8859-1/LC_MESSAGES
	ln -sf ../../fr_FR.ISO8859-1/LC_MESSAGES/LC_MESSAGES \
		${LOCALEDIR}/${link}.ISO8859-1/LC_MESSAGES/LC_MESSAGES
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
		${LOCALEDIR}/${link}.ISO8859-15/LC_MESSAGES
	ln -sf ../../fr_FR.ISO8859-1/LC_MESSAGES/LC_MESSAGES \
		${LOCALEDIR}/${link}.ISO8859-15/LC_MESSAGES/LC_MESSAGES
.endfor
.for link in ${DE_LINKS}
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
		${LOCALEDIR}/${link}.ISO8859-1/LC_MESSAGES
	ln -sf ../../de_DE.ISO8859-1/LC_MESSAGES/LC_MESSAGES \
		${LOCALEDIR}/${link}.ISO8859-1/LC_MESSAGES/LC_MESSAGES
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
		${LOCALEDIR}/${link}.ISO8859-15/LC_MESSAGES
	ln -sf ../../de_DE.ISO8859-1/LC_MESSAGES/LC_MESSAGES \
		${LOCALEDIR}/${link}.ISO8859-15/LC_MESSAGES/LC_MESSAGES
.endfor
.for link in ${IT_LINKS}
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
		${LOCALEDIR}/${link}.ISO8859-1/LC_MESSAGES
	ln -sf ../../it_IT.ISO8859-1/LC_MESSAGES/LC_MESSAGES \
		${LOCALEDIR}/${link}.ISO8859-1/LC_MESSAGES/LC_MESSAGES
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
		${LOCALEDIR}/${link}.ISO8859-15/LC_MESSAGES
	ln -sf ../../it_IT.ISO8859-1/LC_MESSAGES/LC_MESSAGES \
		${LOCALEDIR}/${link}.ISO8859-15/LC_MESSAGES/LC_MESSAGES
.endfor
.for link in ${NL_LINKS}
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
		${LOCALEDIR}/${link}.ISO8859-1/LC_MESSAGES
	ln -sf ../../nl_NL.ISO8859-1/LC_MESSAGES/LC_MESSAGES \
		${LOCALEDIR}/${link}.ISO8859-1/LC_MESSAGES/LC_MESSAGES
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
		${LOCALEDIR}/${link}.ISO8859-15/LC_MESSAGES
	ln -sf ../../nl_NL.ISO8859-1/LC_MESSAGES/LC_MESSAGES \
		${LOCALEDIR}/${link}.ISO8859-15/LC_MESSAGES/LC_MESSAGES
.endfor
.for link in ${CZ_LINKS}
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
		${LOCALEDIR}/${link}.ISO8859-2/LC_MESSAGES
	ln -sf ../../cs_CZ.ISO8859-2/LC_MESSAGES/LC_MESSAGES \
		${LOCALEDIR}/${link}.ISO8859-2/LC_MESSAGES/LC_MESSAGES
.endfor
.for link in ${PT_LINKS}
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
		${LOCALEDIR}/${link}.ISO8859-1/LC_MESSAGES
	ln -sf ../../pt_PT.ISO8859-1/LC_MESSAGES/LC_MESSAGES \
		${LOCALEDIR}/${link}.ISO8859-1/LC_MESSAGES/LC_MESSAGES
.endfor
.for link in ${ASCIILINKS}
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
		${LOCALEDIR}/${link}.US-ASCII/LC_MESSAGES
	ln -sf ../../${link}.ISO8859-1/LC_MESSAGES/LC_MESSAGES \
		${LOCALEDIR}/${link}.US-ASCII/LC_MESSAGES/LC_MESSAGES
.endfor
	${INSTALL} -d -m 755 -o ${BINOWN} -g ${BINGRP} \
		${LOCALEDIR}/lt_LT.ISO8859-4/LC_MESSAGES
	ln -sf ../../lt_LT.ISO8859-13/LC_MESSAGES/LC_MESSAGES \
		${LOCALEDIR}/lt_LT.ISO8859-4/LC_MESSAGES/LC_MESSAGES
	rm -f *.out

.include <bsd.prog.mk>
