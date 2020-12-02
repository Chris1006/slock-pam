# slock-pam - simple screen locker with PAM support.
# See LICENSE file for copyright and license details.

include config.mk

SRC = slock-pam.c ${COMPATSRC}
OBJ = ${SRC:.c=.o}

all: options slock-pam

options:
	@echo slock-pam build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${OBJ}: config.h config.mk

slock-pam: ${OBJ}
	@echo CC -o $@
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	@echo cleaning
	@rm -f slock-pam ${OBJ} slock-pam-${VERSION}.tar.gz

dist: clean
	@echo creating dist tarball
	@mkdir -p slock-pam-${VERSION}
	@cp -R LICENSE Makefile README config.def.h config.mk ${SRC} slock-pam-${VERSION}
	@tar -cf slock-pam-${VERSION}.tar slock-pam-${VERSION}
	@gzip slock-pam-${VERSION}.tar
	@rm -rf slock-pam-${VERSION}

install: all
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f slock-pam ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/slock-pam

uninstall:
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/slock-pam

.PHONY: all options clean dist install uninstall
