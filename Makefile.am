bin_PROGRAMS=distrorun
nobase_sysconf_DATA=distrorun.d/somedistro.conf.example
EXTRA_DIST=distrorun.d

distrorun_SOURCES=distrorun.cpp
distrorun_CPPFLAGS=$(LIBCONFIGXX_CFLAGS) -DSYSCONFDIR="\"${sysconfdir}\""
distrorun_LDADD=$(LIBCONFIGXX_LIBS)

install-exec-hook:
	chown root:root $(DESTDIR)$(bindir)/distrorun$(EXEEXT)
	chmod u+s $(DESTDIR)$(bindir)/distrorun$(EXEEXT)

distrorun$(EXEEXT): $(distrorun_OBJECTS) $(distrorun_DEPENDENCIES) $(EXTRA_distrorun_DEPENDENCIES) 
	@rm -f distrorun$(EXEEXT)
	$(CXXLD) -o$@ $(distrorun_OBJECTS) -Wl,--static $(distrorun_LDADD) -Wl,-Bdynamic $(LIBS)
