noinst_LTLIBRARIES = libgda_threadwrapper-5.0.la

AM_CPPFLAGS = \
        -I$(top_srcdir) -I$(srcdir)/.. \
	-I$(top_srcdir)/libgda/sqlite \
        -I$(top_builddir) \
        $(COREDEPS_CFLAGS) \
	$(COREDEPS_WFLAGS)

libgda_threadwrapper_headers = \
	gda-thread-wrapper.h

libgda_threadwrapperincludedir=$(includedir)/libgda-$(GDA_ABI_MAJOR_VERSION).$(GDA_ABI_MINOR_VERSION)/libgda/thread-wrapper
libgda_threadwrapperinclude_HEADERS=$(libgda_threadwrapper_headers)

libgda_threadwrapper_5_0_la_SOURCES = \
	$(libgda_threadwrapper_headers) \
	gda-thread-blob-op.h \
	gda-thread-blob-op.c \
	gda-thread-meta.h \
	gda-thread-meta.c \
	gda-thread-provider.h \
	gda-thread-provider.c \
	gda-thread-recordset.h \
	gda-thread-recordset.c \
	gda-thread-wrapper.c
