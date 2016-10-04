sbin_PROGRAMS = mapper

mapper_SOURCES = libmapper/app.c
mapper_LDFLAGS = $(SYSTEMD_LIBS)
mapper_CFLAGS = $(SYSTEMD_CFLAGS)
mapper_LDADD = libmapper.la

libmapperdir = ${libdir}
libmapper_LTLIBRARIES = libmapper.la
libmapper_la_SOURCES = libmapper/mapper.c
libmapper_la_LDFLAGS = $(SYSTEMD_LIBS) -version-info 1:0:0 -shared
libmapper_la_CFLAGS = $(SYSTEMD_CFLAGS)

nobase_oldinclude_HEADERS = libmapper/mapper.h

install-exec-hook:
	$(PYTHON) setup.py build ${DISTUTILS_BUILD_ARGS} install ${DISTUTILS_INSTALL_ARGS}

#SUBDIRS = . obmc/mapper


