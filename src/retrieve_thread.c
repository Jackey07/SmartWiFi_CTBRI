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
#include <resolv.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

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

SSL_CTX* InitCTX(void)
{   SSL_METHOD *method;
    SSL_CTX *ctx;

    OpenSSL_add_all_algorithms();  /* Load cryptos, et.al. */
    SSL_load_error_strings();   /* Bring in and register error messages */
    method = SSLv3_client_method();  /* Create new client-method instance */
    ctx = SSL_CTX_new(method);   /* Create new context */
    return ctx;
}

int LoadCertificates(SSL_CTX* ctx, char* CertFile, char* KeyFile)
{
 /* set the local certificate from CertFile */
    if ( SSL_CTX_use_certificate_file(ctx, CertFile, SSL_FILETYPE_PEM) <= 0 )
    {
	return -1;
    }
    /* set the private key from KeyFile (may be the same as CertFile) */
    if ( SSL_CTX_use_PrivateKey_file(ctx, KeyFile, SSL_FILETYPE_PEM) <= 0 )
    {
        return -1;
    }
    /* verify private key */
    if ( !SSL_CTX_check_private_key(ctx) )
    {
        return -1;
    }
}


void confirmTask();
/** @internal
 * This function does the actual request.
 */
void
retrieve(cJSON *auth_json)
{
        ssize_t			numbytes;
        size_t	        	totalbytes;
	int		sockfd, nfds, done;

	char			request[MAX_BUF];
	fd_set			readfds;
	struct timeval		timeout;
	FILE * fh;
	char  *str = NULL;
    	cJSON *json=auth_json;
	SSL *ssl;
	SSL_CTX *ctx;

	SSL_library_init();
	if((ctx = InitCTX())==NULL){
	}

	char CertFile[] = "/home/huangzhe/server.includesprivatekey.pem";
	char KeyFile[] = "/home/huangzhe/server.includesprivatekey.pem";

	if (LoadCertificates(ctx, CertFile, KeyFile)==-1){
		return;
	}




	sockfd = connect_log_server_ssl();
	if (sockfd == -1) {
		return;
	}

   ssl = SSL_new(ctx);      /* create new SSL connection state */
   SSL_set_fd(ssl, sockfd);    /* attach the socket descriptor */

   if ( SSL_connect(ssl) == -1 )  {
	   return;
   }
	snprintf(request, sizeof(request) - 1,
			"GET %s/?gw_id=%s&dev_id=%s HTTP/1.0\r\n"
			"User-Agent: WiFiDog %s\r\n"
			"Host: %s\r\n"
			"\r\n",
			"http://124.127.116.177/taskrequest.json",
			config_get_config()->gw_mac,
			config_get_config()->dev_id,
			VERSION,
			"124.127.116.177");

	
    SSL_write(ssl, request, strlen(request));   /* encrypt & send message */

	
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
			numbytes = SSL_read(ssl, request + totalbytes, MAX_BUF - (totalbytes + 1));
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

			if (format = cJSON_GetObjectItem(json,"task")){
				char *task_code = cJSON_GetObjectItem(format,"task_code")->valuestring;
				char *task_param = cJSON_GetObjectItem(format,"task_param")->valuestring;
				//debug(LOG_DEBUG," %s %s\n", task_code,task_param);    
				confirmTask();
		snprintf(request, sizeof(request) - 1,
			"%s%s",
			task_code,
			task_param);

			execute(request,0);
				
			}
		}
	}

	}
    
    SSL_free(ssl);        /* release connection state */
    SSL_CTX_free(ctx);        /* release context */

	
	debug(LOG_DEBUG, "Done reading reply, total %d bytes", totalbytes);
	close(sockfd);
	return;	
}


void confirmTask(){


	char			request[MAX_BUF];


	SSL *ssl;
	SSL_CTX *ctx;

	SSL_library_init();
	if((ctx = InitCTX())==NULL){
	}

	char CertFile[] = "/home/huangzhe/server.includesprivatekey.pem";
	char KeyFile[] = "/home/huangzhe/server.includesprivatekey.pem";

	if (LoadCertificates(ctx, CertFile, KeyFile)==-1){
		return;
	}




	int 	sockfd = connect_log_server_ssl();
	if (sockfd == -1) {
		return;
	}

   ssl = SSL_new(ctx);      /* create new SSL connection state */
   SSL_set_fd(ssl, sockfd);    /* attach the socket descriptor */

   if ( SSL_connect(ssl) == -1 )  {
	   return;
   }
	snprintf(request, sizeof(request) - 1,
			"GET %s/?gw_id=%s&dev_id=%s HTTP/1.0\r\n"
			"User-Agent: WiFiDog %s\r\n"
			"Host: %s\r\n"
			"\r\n",
			"http://124.127.116.177/taskrequest.json",
			config_get_config()->gw_mac,
			config_get_config()->dev_id,
			VERSION,
			"124.127.116.177");

	
    SSL_write(ssl, request, strlen(request));   /* encrypt & send message */

    SSL_free(ssl);        /* release connection state */
    SSL_CTX_free(ctx);        /* release context */

	
	close(sockfd);
	return;	

}


