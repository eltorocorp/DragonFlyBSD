#$FreeBSD: head/rescue/rescue/Makefile 252356 2013-06-28 21:00:08Z davide $
#	@(#)Makefile	8.1 (Berkeley) 6/2/93

PROG=	rescue.libcrypto

CRUNCH_SRCDIRS+= sbin
CRUNCH_PROGS_sbin= tcplay	\
		    md5		\
		    hammer	\
		    hammer2	\
		    ping	\
		    ping6

CRUNCH_SRCDIRS+= gnu/sbin
CRUNCH_PROGS_gnu/sbin= cryptsetup

CRUNCH_LIBS+= -ldm -lprop -ldmsg -lutil -lhammer -lpthread -lm
CRUNCH_ALIAS_md5= rmd160

CRUNCH_SRCDIRS+= usr.sbin/802_11
CRUNCH_PROGS_usr.sbin/802_11= wpa_supplicant
CRUNCH_LIBS+= -lpcap -lprivate_ssl -lmd -lprivate_crypto
CRUNCH_LIBS+= ${PRIVATELIB_LDFLAGS}

.include <bsd.crunchgen.mk>
.include <bsd.prog.mk>
