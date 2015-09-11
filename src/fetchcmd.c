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

/* $Id$ */
/** @file ping_thread.c
    @brief Periodically checks in with the central auth server so the auth
    server knows the gateway is still up.  Note that this is NOT how the gateway
    detects that the central server is still up.
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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>

#include "../config.h"
#include "safe.h"
#include "common.h"
#include "conf.h"
#include "debug.h"
#include "retrieve_thread.h"
#include "util.h"
#include "centralserver.h"
#include "firewall.h"
#include "cJSON.h"

void confirmTasking(const char *task_id);

void
fetchcmd()
{
        ssize_t			numbytes;
        size_t	        	totalbytes;
	int		sockfd, nfds, done;

	char			request[MAX_BUF];
	fd_set			readfds;
	struct timeval		timeout;
	FILE * fh;
	char  *str = NULL;
    	cJSON *json;

	sockfd = connect_auth_server();
	if (sockfd == -1) {
		return;
	}

    snprintf(request, sizeof(request) - 1,
		    "GET %s?dev_id=%s HTTP/1.0\r\n"
		    "User-Agent: WiFiDog %s\r\n"
		    "Host: %s\r\n"
		    "\r\n",
		    "/api10/taskrequest",
		    config_get_config()->dev_id,
		    VERSION,
		    get_auth_server()->serv_hostname);
	
	debug(LOG_DEBUG, "Sending cnd request to auth server:%s\n", request);

	
	send(sockfd, request, strlen(request), 0);
	numbytes = totalbytes = 0;
	done = 0;
	do {
		FD_ZERO(&readfds);
		FD_SET(sockfd, &readfds);
		timeout.tv_sec = 30; /* XXX magic... 30 second */
		timeout.tv_usec = 0;
		nfds = sockfd + 1;

		nfds = select(nfds, &readfds, NULL, NULL, &timeout);

		if (nfds > 0) {
			/** We don't have to use FD_ISSET() because there
			 *  was only one fd. */
			numbytes = read(sockfd, request + totalbytes, MAX_BUF - (totalbytes + 1));
			if (numbytes < 0) {
				debug(LOG_ERR, "An error occurred while reading from auth server: %s", strerror(errno));
				/* FIXME */
				close(sockfd);
				return;
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
			/* FIXME */
			close(sockfd);
			return;
		}
		else if (nfds < 0) {
			debug(LOG_ERR, "Error reading data via select() from auth server: %s", strerror(errno));
			/* FIXME */
			close(sockfd);
			return;
		}
	} while (!done);


	request[totalbytes] = '\0';
	debug(LOG_DEBUG," %s \n",request);    

    str = strstr(request, "{");
    if (str != 0) {
	
    	json=cJSON_Parse(str);
	if (!json) {debug(LOG_DEBUG,"Error before: [%s]\n",cJSON_GetErrorPtr());}
	else{
		if (cJSON_GetObjectItem(json,"result")  && 
			strcmp(cJSON_GetObjectItem(json,"result")->valuestring,"OK")==0){
    			cJSON *format;
			debug(LOG_DEBUG,"task1 \n");    

			if (format = cJSON_GetObjectItem(json,"task")){
				debug(LOG_DEBUG,"task2 \n");    
				
				char *task_id = cJSON_GetObjectItem(format,"task_id")->valuestring;
				int task_code = cJSON_GetObjectItem(format,"task_code")->valueint;
				confirmTasking(task_id);
				debug(LOG_DEBUG,"task_code %d \n",task_code);    
				if(task_code==1000){
					execute("reboot",0);
					debug(LOG_DEBUG," reboot \n");    
				}else if(task_code == 2002){
					//execute("smctl restart",0);
					debug(LOG_DEBUG," smctl restart \n");    
					fw_destroy();
					if (!fw_init()) {
						debug(LOG_ERR, "FATAL: Failed to initialize firewall");
						exit(1);
					}
				}else if(task_code ==2003){
					cJSON * task = cJSON_GetObjectItem(format,"task_params");
					char *hostname = cJSON_GetObjectItem(task,"hostname")->valuestring;
					char *ssid = cJSON_GetObjectItem(task,"ssid")->valuestring;
					debug(LOG_DEBUG,"%s %s \n",hostname,ssid);    
					ssidEdit(ssid);
					hostnameEdit(hostname);
					execute("/etc/init.d/network restart  >/dev/null 2>&1;smctl restart;",0);
					
				}else if(task_code == 3000){
					char	sysupgrade[200];
					snprintf(sysupgrade, sizeof(sysupgrade) - 1,
							"sysupgrade -b /etc/back.tar.gz;sysupgrade -f /etc/back.tar.gz  -v %s",
							config_get_config()->imageurl);
						
					debug(LOG_DEBUG,"%s \n",sysupgrade);
					execute(sysupgrade,0);    
					
				}
			}
		}
	}

	}
    

	
	close(sockfd);
	return;	
}


void confirmTasking(const char *task_id){

    unsigned int	        totalbytes;
    int			nfds, done;
    fd_set			readfds;
    unsigned int		numbytes;
	struct timeval		timeout;
	char			request[MAX_BUF];


int 	sockfd = connect_auth_server();


	if (sockfd == -1) {
		return;
	}

	snprintf(request, sizeof(request) - 1,
			"GET %s?dev_id=%s&task_id=%s&result=%s&message=12345678 HTTP/1.0\r\n"
			"User-Agent: WiFiDog %s\r\n"
			"Host: %s\r\n"
			"\r\n",
			"/api10/taskresult",
		    	config_get_config()->dev_id,
			task_id,
			"OK",
			"1.1",
		    get_auth_server()->serv_hostname);

	
	debug(LOG_DEBUG," %s \n",request);    
	send(sockfd, request, strlen(request), 0);
	
	numbytes = totalbytes = 0;
	done = 0;
	do {
		FD_ZERO(&readfds);
		FD_SET(sockfd, &readfds);
		timeout.tv_sec = 30; /* XXX magic... 30 second */
		timeout.tv_usec = 0;
		nfds = sockfd + 1;

		nfds = select(nfds, &readfds, NULL, NULL, &timeout);

		if (nfds > 0) {
			numbytes = read(sockfd, request + totalbytes, MAX_BUF - (totalbytes + 1));

			if (numbytes < 0) {
				close(sockfd);
				return;
			}
			else if (numbytes == 0) {
				done = 1;
			}
			else {
				totalbytes += numbytes;
			}
		}
		else if (nfds == 0) {
			close(sockfd);
			return;
		}
		else if (nfds < 0) {
			close(sockfd);
			return;
		}
	} while (!done);


    request[totalbytes] = '\0';
	debug(LOG_DEBUG," %s \n",request);    
	close(sockfd);
	return;	

}


