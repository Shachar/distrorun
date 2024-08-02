bin_PROGRAMS=distrorun
nobase_sysconf_DATA=distrorun.d/somedistro.conf.example

distrorun_SOURCES=distrorun.cpp
distrorun_CPPFLAGS=$(LIBCONFIGXX_CFLAGS) -DSYSCONFDIR="\"${sysconfdir}\""
distrorun_LDADD=$(LIBCONFIGXX_LIBS)
