#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(cciss_vol_status, 1.03, smcameron@users.sourceforge.net)
AC_CONFIG_SRCDIR([cciss_vol_status.c])
AC_CONFIG_HEADER([config.h])

AM_INIT_AUTOMAKE()
# Checks for programs.
AC_PROG_CC

# Checks for libraries.

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([errno.h fcntl.h inttypes.h netinet/in.h stdlib.h string.h sys/ioctl.h unistd.h])
AC_CHECK_HEADER(scsi/sg.h, AC_DEFINE([HAVE_SCSI_SG_H], 1, [1 if Linux SCSI Generic header file present]), AC_MSG_WARN([scsi/sg.h not found so MSA1000 support will not be compiled.]))
AC_CHECK_HEADER(scsi/scsi.h, AC_DEFINE([HAVE_SCSI_SCSI_H], 1, [1 if Linux SCSI header file present]))
AC_CHECK_HEADER(linux/cciss_ioctl.h, AC_DEFINE([HAVE_LINUX_CCISS_IOCTL_H], 1, [1 if Linux cciss driver ioctl header file present]))
AC_CHECK_HEADER(/usr/src/sys/dev/ciss/cissio.h, AC_DEFINE([HAVE_CISSIO_H], 1, [1 if FreeBSD ciss driver ioctl header file present]))

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

# Checks for library functions.
AC_PROG_GCC_TRADITIONAL
AC_FUNC_MALLOC
AC_CHECK_FUNC(getopt)
AC_CHECK_FUNC(getopt_long)
AC_CHECK_FUNCS([memset strerror strstr])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
