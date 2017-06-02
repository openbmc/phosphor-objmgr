AM_DEFAULT_SOURCE_EXT = .cpp
AM_CPPFLAGS = -I$(top_srcdir)

sbin_PROGRAMS = \
	phosphor-unit-failure-monitor

phosphor_unit_failure_monitor_SOURCES = \
	main.cpp \
	monitor.cpp \
	../xyz/openbmc_project/Fail/Monitor/error.cpp

phosphor_unit_failure_monitor_LDADD = \
	$(PHOSPHOR_LOGGING_LIBS) \
	$(SDBUSPLUS_LIBS)

phosphor_unit_failure_monitor_CXXFLAGS = \
	$(PHOSPHOR_LOGGING_CFLAGS) \
	$(SDBUSPLUS_CFLAGS)

nobase_nodist_include_HEADERS = \
	${top_srcdir}/xyz/openbmc_project/Fail/Monitor/error.hpp

BUILT_SOURCES = \
	${top_srcdir}/xyz/openbmc_project/Fail/Monitor/error.cpp \
	${top_srcdir}/xyz/openbmc_project/Fail/Monitor/error.hpp

CLEANFILES = ${BUILT_SOURCES}

${top_srcdir}/xyz/openbmc_project/Fail/Monitor/error.hpp: ${top_srcdir}/xyz/openbmc_project/Fail/Monitor.errors.yaml
	@mkdir -p `dirname $@`
	$(SDBUSPLUSPLUS) -r $(top_srcdir) error exception-header xyz.openbmc_project.Fail.Monitor > $@

${top_srcdir}/xyz/openbmc_project/Fail/Monitor/error.cpp: ${top_srcdir}/xyz/openbmc_project/Fail/Monitor.errors.yaml
	@mkdir -p `dirname $@`
	$(SDBUSPLUSPLUS) -r $(top_srcdir) error exception-cpp xyz.openbmc_project.Fail.Monitor > $@
