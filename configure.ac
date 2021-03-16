m4_define([v4l2_relayd_major_version], [0])
m4_define([v4l2_relayd_minor_version], [1])
m4_define([v4l2_relayd_micro_version], [0])
m4_define([v4l2_relayd_interface_age], [1])
m4_define([v4l2_relayd_binary_age],
          [m4_eval(100 * v4l2_relayd_minor_version + v4l2_relayd_micro_version)])
m4_define([v4l2_relayd_version],
          [v4l2_relayd_major_version.v4l2_relayd_minor_version.v4l2_relayd_micro_version])
m4_define([v4l2_relayd_api_version], [0.0])

AC_PREREQ([2.62])
AC_INIT([v4l2-relayd], [v4l2_relayd_version],
        [https://gitlab.com/vicamo/v4l2-relayd/-/issues],
        [v4l2-relayd],
        [https://gitlab.com/vicamo/v4l2-relayd])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])

# Depends on automake 1.14 or newer so that we don't need AM_PROG_CC_C_O
AM_INIT_AUTOMAKE([1.14 dist-xz foreign subdir-objects no-define -Wall])
AM_MAINTAINER_MODE([enable])
AM_SILENT_RULES([yes])

V4L2_RELAYD_MAJOR_VERSION=v4l2_relayd_major_version
V4L2_RELAYD_MINOR_VERSION=v4l2_relayd_minor_version
V4L2_RELAYD_MICRO_VERSION=v4l2_relayd_micro_version
V4L2_RELAYD_INTERFACE_AGE=v4l2_relayd_interface_age
V4L2_RELAYD_BINARY_AGE=v4l2_relayd_binary_age
V4L2_RELAYD_VERSION=v4l2_relayd_version
V4L2_RELAYD_API_VERSION=v4l2_relayd_api_version
AC_SUBST(V4L2_RELAYD_MACRO_VERSION)
AC_SUBST(V4L2_RELAYD_MINOR_VERSION)
AC_SUBST(V4L2_RELAYD_MICRO_VERSION)
AC_SUBST(V4L2_RELAYD_INTERFACE_AGE)
AC_SUBST(V4L2_RELAYD_BINARY_AGE)
AC_SUBST(V4L2_RELAYD_VERSION)
AC_SUBST(V4L2_RELAYD_API_VERSION)

m4_define([lt_current], [m4_eval(100 * v4l2_relayd_minor_version + v4l2_relayd_micro_version - v4l2_relayd_interface_age)])
m4_define([lt_revision], [v4l2_relayd_interface_age])
m4_define([lt_age], [m4_eval(v4l2_relayd_binary_age - v4l2_relayd_interface_age)])
LT_VERSION_INFO="lt_current:lt_revision:lt_age"
LT_CURRENT_MINUS_AGE=m4_eval(lt_current - lt_age)
AC_SUBST(LT_VERSION_INFO)
AC_SUBST(LT_CURRENT_MINUS_AGE)

AX_CHECK_ENABLE_DEBUG

AC_PROG_CC

# Define _GNU_SOURCE etc. where appropriate, e.g. for strptime() on glibc
AC_USE_SYSTEM_EXTENSIONS
# Make sure we use 64-bit versions of various file stuff.
AC_SYS_LARGEFILE

dnl Initialize libtool
LT_PREREQ([2.2.6])
LT_INIT([disable-static])
# Create libtool early, because it's used in configure
LT_OUTPUT

GIO_UNIX_REQUIRED=2.36
PKG_CHECK_MODULES(DEPS, [
  glib-2.0
  gio-unix-2.0 >= $GIO_UNIX_REQUIRED
])

dnl required versions of gstreamer and plugins-base
GST_REQUIRED=1.0.0
GSTPB_REQUIRED=1.0.0

PKG_CHECK_MODULES(GST, [
  gstreamer-1.0 >= $GST_REQUIRED
  gstreamer-app-1.0 >= $GSTPB_REQUIRED
  gstreamer-video-1.0 >= $GSTPB_REQUIRED
], [
  AC_SUBST(GST_CFLAGS)
  AC_SUBST(GST_LIBS)
], [
  AC_MSG_ERROR([
      You need to install or upgrade the GStreamer development
      packages on your system. On debian-based systems these are
      libgstreamer1.0-dev; on RPM-based systems gstreamer1.0-devel.
      The minimum version required is $GST_REQUIRED.
  ])
])

AC_CONFIG_FILES([
  Makefile
])

AC_OUTPUT
