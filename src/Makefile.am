#
# $Id$
#

bin_PROGRAMS = smartwifi \
	smctl
 
AM_CPPFLAGS = \
	-I${top_srcdir}/libhttpd/ \
	-DSYSCONFDIR='"$(sysconfdir)"' 
smartwifi_LDADD = $(top_builddir)/libhttpd/libhttpd.la -lm -lssl -lcrypto -luci

smartwifi_SOURCES = commandline.c \
	conf.c \
	debug.c \
	fw_iptables.c \
	firewall.c \
	gateway.c \
	centralserver.c \
	http.c \
	auth.c \
	authlog.c \
	client_list.c \
	util.c \
	wdctl_thread.c \
	fetchcmd.c \
	ping_thread.c \
	ding_thread.c \
	retrieve_thread.c \
	cJSON.c \
	safe.c \
	httpd_thread.c \
	update.c

noinst_HEADERS = commandline.h \
	common.h \
	conf.h \
	debug.h \
	fw_iptables.h \
	firewall.h \
	gateway.h \
	centralserver.h \
	http.h \
	auth.h \
	authlog.h \
	client_list.h \
	util.h \
	wdctl_thread.h \
	fetchcmd.h \
	wdctl.h \
	ping_thread.h \
	ding_thread.h \
	safe.h \
	retrieve_thread.h \
	cJSON.h \
	httpd_thread.h \
	update.h

smctl_SOURCES = wdctl.c
