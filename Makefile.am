sbin_PROGRAMS = mapper mapperx

mapper_SOURCES = libmapper/app.c
mapper_LDFLAGS = $(SYSTEMD_LIBS)
mapper_CFLAGS = $(SYSTEMD_CFLAGS)
mapper_LDADD = libmapper.la

libmapperdir = ${libdir}
libmapper_LTLIBRARIES = libmapper.la
libmapper_la_SOURCES = libmapper/mapper.c
libmapper_la_LDFLAGS = $(SYSTEMD_LIBS) -version-info 1:0:0 -shared
libmapper_la_CFLAGS = $(SYSTEMD_CFLAGS)

mapperx_SOURCES = src/main.cpp src/argument.cpp src/processing.cpp
mapperx_LDFLAGS = $(SDBUSPLUS_LIBS) -pthread -ltinyxml2
mapperx_CXXFLAGS = $(SYSTEMD_CFLAGS) -DBOOST_SYSTEM_NO_DEPRECATED -DBOOST_ERROR_CODE_HEADER_ONLY -DBOOST_ALL_NO_LIB

include_HEADERS = libmapper/mapper.h

if HAVE_PYTHON

all-local:
	$(AM_V_at)$(PYTHON) ${top_builddir}/setup.py build

clean-local:
	rm -rfv ${top_builddir}/build

install-exec-hook:
	$(AM_V_at)$(PYTHON) ${top_builddir}/setup.py install \
	--prefix=$(DESTDIR)${prefix} \
	--install-data=$(DESTDIR)${datadir} \
	--install-lib=$(DESTDIR)$(PYTHONDIR) \
	--install-scripts=$(DESTDIR)${sbindir} \
	--record=${top_builddir}/python_install.log

uninstall-hook:
	cat ${top_builddir}/python_install.log \
	| $(AWK) '{print "$(DESTDIR)"$$1}' | xargs rm -fv

endif

SUBDIRS = fail-monitor

@CODE_COVERAGE_RULES@

check_PROGRAMS =
XFAIL_TESTS =

AM_LIBS = $(CODE_COVERAGE_LIBS)
AM_CPPFLAGS = $(CODE_COVERAGE_CPPFLAGS) -UNDEBUG $(GTEST_CPPFLAGS)
AM_CFLAGS = $(CODE_COVERAGE_CFLAGS)
AM_CXXFLAGS = $(CODE_COVERAGE_CXXFLAGS)
AM_LDFLAGS = $(GMOCK_LIBS) -lgmock_main \
	$(GTEST_LIBS) $(OESDK_TESTCASE_FLAGS) $(PTHREAD_LIBS)

include src/test/Makefile.am.include

TESTS = $(check_PROGRAMS)

