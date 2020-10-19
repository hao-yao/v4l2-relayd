ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

SUBDIRS = .

bin_PROGRAMS =

EXTRA_DIST = \
  autogen.sh \
  $(empty)

GITIGNOREFILES = \
  $(DIST_ARCHIVES) \
  $(empty)

MAINTAINERCLEANFILES = \
  $(GITIGNORE_MAINTAINERCLEANFILES_TOPLEVEL) \
  $(GITIGNORE_MAINTAINERCLEANFILES_M4_LIBTOOL) \
  $(GITIGNORE_MAINTAINERCLEANFILES_MAKEFILE_IN) \
  $(srcdir)/ChangeLog \
  $(srcdir)/INSTALL \
  $(srcdir)/m4/gtk-doc.m4 \
  $(srcdir)/README.md \
  $(empty)

DISTCHECK_CONFIGURE_FLAGS = \
  --disable-maintainer-mode \
  $(empty)

CLEANFILES = \
  $(empty)

AM_CPPFLAGS = \
  -DG_LOG_DOMAIN=\"v4l2_relayd\" \
  -DV4L2_RELAYD_VERSION=\"$(V4L2_RELAYD_VERSION)\" \
  -I$(top_builddir) \
  -I$(top_srcdir)

AM_CFLAGS = \
  -Wall -Werror

###############################
## v4l2_relayd

bin_PROGRAMS += \
  src/v4l2-relayd

src_v4l2_relayd_SOURCES = \
  src/v4l2-relayd.c
src_v4l2_relayd_CFLAGS = \
  $(AM_CFLAGS) \
  $(DEPS_CFLAGS) \
  $(empty)
src_v4l2_relayd_LDADD = \
  $(DEPS_LIBS) \
  $(empty)

distclean-local:
	if test "$(srcdir)" = "."; then :; else \
	  rm -f ChangeLog; \
	fi

.PHONY: ChangeLog
ChangeLog:
	$(AM_V_GEN) if test -d "$(srcdir)/.git"; then \
	  (GIT_DIR=$(top_srcdir)/.git $(top_builddir)/build-aux/missing git log --stat) | fmt --split-only > $@.tmp \
	  && mv -f $@.tmp $@ \
	  || ($(RM) $@.tmp; \
	      echo Failed to generate ChangeLog, your ChangeLog may be outdated >&2; \
	      (test -f $@ || echo git-log is required to generate this file >> $@)); \
	else \
	  test -f $@ || \
	  (echo A git checkout and git-log is required to generate ChangeLog >&2 && \
	  echo A git checkout and git-log is required to generate this file >> $@); \
	fi

CLEANFILES += $(BUILT_SOURCES)
GITIGNOREFILES += $(BUILT_SOURCES)

-include $(top_srcdir)/git.mk
