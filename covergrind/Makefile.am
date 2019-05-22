include $(top_srcdir)/Makefile.tool.am

#----------------------------------------------------------------------------
# Headers, etc
#----------------------------------------------------------------------------

bin_SCRIPTS = cv_annotate

noinst_HEADERS = cv_hashtable.h

#----------------------------------------------------------------------------
# covergrind-<platform>
#----------------------------------------------------------------------------

noinst_PROGRAMS  = covergrind-@VGCONF_ARCH_PRI@-@VGCONF_OS@
if VGCONF_HAVE_PLATFORM_SEC
noinst_PROGRAMS += covergrind-@VGCONF_ARCH_SEC@-@VGCONF_OS@
endif

covergrind_SOURCES_COMMON = \
	cv_main.c \
	cv_hashtable.c

covergrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_SOURCES      = \
	$(covergrind_SOURCES_COMMON)
covergrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_CPPFLAGS     = \
	$(AM_CPPFLAGS_@VGCONF_PLATFORM_PRI_CAPS@)
covergrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_CFLAGS       = $(LTO_CFLAGS) \
	$(AM_CFLAGS_@VGCONF_PLATFORM_PRI_CAPS@)
covergrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_DEPENDENCIES = \
	$(TOOL_DEPENDENCIES_@VGCONF_PLATFORM_PRI_CAPS@)
covergrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LDADD        = \
	$(TOOL_LDADD_@VGCONF_PLATFORM_PRI_CAPS@)
covergrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LDFLAGS      = \
	$(TOOL_LDFLAGS_@VGCONF_PLATFORM_PRI_CAPS@)
covergrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LINK = \
	$(top_builddir)/coregrind/link_tool_exe_@VGCONF_OS@ \
	@VALT_LOAD_ADDRESS_PRI@ \
	$(LINK) \
	$(covergrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_CFLAGS) \
	$(covergrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LDFLAGS)

if VGCONF_HAVE_PLATFORM_SEC
covergrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_SOURCES      = \
	$(covergrind_SOURCES_COMMON)
covergrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_CPPFLAGS     = \
	$(AM_CPPFLAGS_@VGCONF_PLATFORM_SEC_CAPS@)
covergrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_CFLAGS       = $(LTO_CFLAGS)\
	$(AM_CFLAGS_@VGCONF_PLATFORM_SEC_CAPS@)
covergrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_DEPENDENCIES = \
	$(TOOL_DEPENDENCIES_@VGCONF_PLATFORM_SEC_CAPS@)
covergrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LDADD        = \
	$(TOOL_LDADD_@VGCONF_PLATFORM_SEC_CAPS@)
covergrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LDFLAGS      = \
	$(TOOL_LDFLAGS_@VGCONF_PLATFORM_SEC_CAPS@)
covergrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LINK = \
	$(top_builddir)/coregrind/link_tool_exe_@VGCONF_OS@ \
	@VALT_LOAD_ADDRESS_SEC@ \
	$(LINK) \
	$(covergrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_CFLAGS) \
	$(covergrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LDFLAGS)
endif
