/********************************************************************\
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 59 Temple Place - Suite 330        Fax:    +1-617-542-2652       *
 * Boston, MA  02111-1307,  USA       gnu@gnu.org                   *
 *                                                                  *
\********************************************************************/

/* $Id: auth.c 1373 2008-09-30 09:27:40Z wichert $ */
/** @file auth.c
    @brief Authentication handling thread
    @author Copyright (C) 2004 Alexandre Carmel-Veilleux <acv@miniguru.ca>
*/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <syslog.h>

#include "httpd.h"
#include "http.h"
#include "safe.h"
#include "conf.h"
#include "debug.h"
#include "auth.h"
#include "centralserver.h"
#include "fw_iptables.h"
#include "firewall.h"
#include "client_list.h"
#include "util.h"
#include "common.h"

/* Defined in clientlist.c */
extern	pthread_mutex_t	client_list_mutex;

/* Defined in util.c */
extern long served_this_session;

/** Launches a thread that periodically checks if any of the connections has timed out
@param arg Must contain a pointer to a string containing the IP adress of the client to check to check
@todo Also pass MAC adress? 
@todo This thread loops infinitely, need a watchdog to verify that it is still running?
*/  
void
log_with_authserver(void);
int
log_server_request(const char *request_type, const char *ip, const char *mac, const char *token, unsigned long long int incoming, unsigned long long int outgoing);

void
thread_client_timeout_log(const void *arg)
{
	pthread_cond_t		cond = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t		cond_mutex = PTHREAD_MUTEX_INITIALIZER;
	struct	timespec	timeout;
	
	while (1) {
		/* Sleep for config.checkinterval seconds... */
		timeout.tv_sec = time(NULL)+30;
		timeout.tv_nsec = 0;

		/* Mutex must be locked for pthread_cond_timedwait... */
		pthread_mutex_lock(&cond_mutex);
		
		/* Thread safe "sleep" */
		pthread_cond_timedwait(&cond, &cond_mutex, &timeout);

		/* No longer needs to be locked */
		pthread_mutex_unlock(&cond_mutex);
	
		debug(LOG_DEBUG, "Running fw_counter()");
	
		log_with_authserver();
	}
}


void
log_with_authserver(void)
{
    char            *ip, *mac, *token;
    t_client        *p1, *p2;
    unsigned long long	    incoming, outgoing;


    LOCK_CLIENT_LIST();

    for (p1 = p2 = client_get_first_client(); NULL != p1; p1 = p2) {
        p2 = p1->next;

        ip = safe_strdup(p1->ip);
        token = safe_strdup(p1->token);
        mac = safe_strdup(p1->mac);
	    outgoing = p1->counters.outgoing;
	    incoming = p1->counters.incoming;
	    UNLOCK_CLIENT_LIST();
            log_server_request(REQUEST_TYPE_COUNTERS, ip, mac, token, incoming, outgoing);
	    LOCK_CLIENT_LIST();
        free(ip);
        free(token);
        free(mac);

     }

	//free(p1);
	//free(p2);

    UNLOCK_CLIENT_LIST();
}

int
log_server_request(const char *request_type, const char *ip, const char *mac, const char *token, unsigned long long int incoming, unsigned long long int outgoing)
{
	int sockfd;
	ssize_t	numbytes;
	size_t totalbytes;
	char buf[MAX_BUF];
	char *tmp;
	int done, nfds;
	fd_set			readfds;
	struct timeval		timeout;
	
	/* Blanket default is error. */
	
	sockfd = connect_log_server();
	if (sockfd == -1) {
		/* Could not connect to any auth server */
		return -1;
	}

	/**
	 * everywhere.
	 */
	memset(buf, 0, sizeof(buf));
        //safe_token=httpdUrlEncode(token);
	snprintf(buf, (sizeof(buf) - 1),
		"GET %s?stage=%s&ip=%s&mac=%s&incoming=%llu&outgoing=%llu&gw_id=%s&token=%s HTTP/1.0\r\n"
		"User-Agent: WiFiDog \r\n"
		"Host: %s\r\n"
		"\r\n",
		"http://Wifi-admin.ctbri.com.cn/auth",
		request_type,
		ip,
		mac,
		incoming,
		outgoing,
                config_get_config()->gw_mac,
		token,
		"124.127.116.177"
	);


	debug(LOG_DEBUG, "Sending HTTP request to auth server: [%s]\n", buf);
	send(sockfd, buf, strlen(buf), 0);

	debug(LOG_DEBUG, "Reading response");
	numbytes = totalbytes = 0;
	done = 0;
	do {
		FD_ZERO(&readfds);
		FD_SET(sockfd, &readfds);
		timeout.tv_sec = 30; /* X magic... 30 second is as good a timeout as any */
		timeout.tv_usec = 0;
		nfds = sockfd + 1;

		nfds = select(nfds, &readfds, NULL, NULL, &timeout);

		if (nfds > 0) {
			/** We don't have to use FD_ISSET() because there
			 *  was only one fd. */
			numbytes = read(sockfd, buf + totalbytes, MAX_BUF - (totalbytes + 1));
			if (numbytes < 0) {
				debug(LOG_ERR, "An error occurred while reading from auth server:");
				close(sockfd);
				return -1;
			}
			else if (numbytes == 0) {
				done = 1;
			}
			else {
				totalbytes += numbytes;
				debug(LOG_DEBUG, "Read %d bytes, total now %d", numbytes, totalbytes);
			}
		}
		else if (nfds == 0) {
			debug(LOG_ERR, "Timed out reading data via select() from auth server");
			close(sockfd);
			return -1;
		}
		else if (nfds < 0) {
			debug(LOG_ERR, "Error reading data via select() from auth server");
			close(sockfd);
			return -1;
		}
	} while (!done);

	close(sockfd);

	buf[totalbytes] = '\0';
	debug(LOG_DEBUG, "HTTP Response from Server: [%s]", buf);
	
	return -1;
}


