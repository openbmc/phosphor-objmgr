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

mapperx_SOURCES = src/main.cpp src/argument.cpp
mapperx_LDFLAGS = $(SDBUSPLUS_LIBS) -pthread -ltinyxml2
mapperx_CXXFLAGS = $(SYSTEMD_CFLAGS) -DBOOST_SYSTEM_NO_DEPRECATED -DBOOST_ERROR_CODE_HEADER_ONLY -DBOOST_ALL_NO_LIB

include_HEADERS = libmapper/mapper.h

SUBDIRS = fail-monitor
