/********************************************************************\
 *
 *
\********************************************************************/

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

#include "safe.h"
#include "common.h"
#include "conf.h"
#include "debug.h"
#include "util.h"
#include "centralserver.h"
#include "fetchcmd.h"
#include "update.h"



void
thread_update(void arg)
{
	pthread_cond_t		cond = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t		cond_mutex = PTHREAD_MUTEX_INITIALIZER;
	struct	timespec	time_to_update;

	while (1) {
		// set a timer to activate the update process


		debug(LOG_DEBUG, "Begining update()");
		if (update()) {
			debug(LOG_DEBUG, "Update failed");
			// make thread sleep?
		}
	}
}

int
update(void)
{
	ssize_t		numbytes;
	size_t		totalbytes;
	int			sockfd, nfds, done;
	char		request[MAX_BUF];
	fd_set		readfds;

	t_serv		*update_server = NULL;

	s_config	*config = NULL;




	// TODO
	update_server = get_update_server();

	config = config_get_config();

	debug(LOG_DEBUG, "Entering update()");

	// TODO
	sockfd = connect_update_server();
	if (sockfd == -1) {
		return -1;
	}

	// http://apupgrade.51awifi.com/upgrade/update.do?devid=DEFAULT_ZJ_0001&version=V1.0.1&model=HG261GS&HDversion=HS.V2.0&supplier=FiberHome&city=310000&applyid=1289820708&rdMD5=a8381eb16324fc69647a19aaeda7b406
	snprintf(request, sizeof(request) - 1,
			"GET %s?devid=%s&version=%s&model=%s&HDversion=%s&supplier=%s&city=%s&applyid=%s&rdMD5=%s HTTP/1.0\r\n"
			"/upgrade/update.do",	// update_server->serv_path,
									// Util.c/urlRead() ...
			"DEFAULT_ZJ_0001",		// config->dev_id,
			"V1.0.1",
			"HG261GS",
			"HS.V2.0",
			"FiberHome",
			"310000",
			"1289820708",
			"a8381eb16324fc69647a19aaeda7b406");

	debug(LOG_DEBUG, "HTTP Request to Server: [%s]", request);

	send(sockfd, request, strlen(request), 0);

	debug(LOG_DEBUG, "Reading response");

	numbytes = totalbytes = 0;
	done = 0;
	do {
		FD_ZERO(&readfds);
		FD_SET(sockfd, &readfds);

		nfds = sockfd + 1;

		nfds = select(nfds, &readfds, NULL, NULL, NULL);

		if (nfds > 0) {
			numbytes = read(sockfd, request + totalbytes, MAX_BUF - (totalbytes + 1));
			if (numbytes < 0) {
				debug(LOG_ERR, "An error occurred while reading from update server: %s", strerror(errno));
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
			debug(LOG_ERR, "Timed out reading data via select() from update server");
			close(sockfd);
			return -1;
		}
		else if (nfds < 0) {
			debug(LOG_ERR, "Error reading data via select() from update server: %s", strerror(errno));
			close(sockfd);
			return -1;
		}
	} while (!done);
	close(sockfd);

	debug(LOG_DEBUG, "Done reading reply, total %d bytes", totalbytes);

	request[totalbytes] = '\0';

	debug(LOG_DEBUG, "HTTP Response from Server: [%s]", request);

	// The correct URL returned is a download address of update file.
	if (strstr(request, ".bin") == 0) {
		if (retrieve_update_file(request)) {
			debug(LOG_DEBUG, "Retrieving update file failed");
			return -1;
		}
	} else {
		debug(LOG_DEBUG, "HTTP request format maybe wrong");
		return -1;
	}

	// TODO network_traffic();

	if (do_update()) {
		debug(LOG_DEBUG, "Sysupgrade failed");
		return -1;
	}


	return 0;
}


int
retrieve_update_file(char *request)
{
	// UPDATE_FILE
	return 0;
}

int
do_update()
{
	// UPDATE_FILE
	return 0;
}























