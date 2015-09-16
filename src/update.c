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
thread_update(void *arg)
{
	pthread_cond_t		cond = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t		cond_mutex = PTHREAD_MUTEX_INITIALIZER;
	struct	timespec	time_to_update;
	time_t				timep;

	while (1) {
		/* Set a timer to activate the update procedure
		 * If current time is not in the update period, recheck it 1 hour later */
		if (in_update_time_period()) {
			time_to_update.tv_sec = time(NULL) + 3600;
			time_to_update.tv_nsec = 0;

			/* Mutex must be locked for pthread_cond_timedwait... */
			pthread_mutex_lock(&cond_mutex);

			/* Thread safe "sleep" */
			pthread_cond_timedwait(&cond, &cond_mutex, &time_to_update);

			/* No longer needs to be locked */
			pthread_mutex_unlock(&cond_mutex);
			continue;
		}		

		if (update()) {
			debug(LOG_DEBUG, "Update failed");
			// TODO make thread sleep?
		}

		timep = time(NULL);
		debug(LOG_DEBUG, "Updated successfully in: %s", asctime(localtime(&timep)));		
	}
}

int
in_update_time_period()
{
	time_t		timep = time(NULL);	
	struct tm	*p_tm = localtime(&timep);
	int			current_hour = (p_tm->tm_hour + 8) % 24;
	
	if ((current_hour < 2) || (current_hour > 5)) {
		debug(LOG_DEBUG, "Current time is not in the period 2:00 - 5:00");
		return -1;
	}

	return 0;
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

	/* Delay sending request for a random time
	 * Using random function with seed which is the last 2 letter of MAC address */
	// TODO

	update_server = get_update_server();
	config = config_get_config();
	debug(LOG_DEBUG, "Entering function update()");

	sockfd = connect_update_server();
	if (sockfd == -1) {
		return -1;
	}

	// http://apupgrade.51awifi.com/upgrade/update.do?devid=DEFAULT_ZJ_0001&version=V1.0.1&model=HG261GS&HDversion=HS.V2.0&supplier=FiberHome&city=310000&applyid=1289820708&rdMD5=a8381eb16324fc69647a19aaeda7b406
	snprintf(request, sizeof(request) - 1,
			"GET http://apupgrade.51awifi.com:80/upgrade/update.do?devid=DEFAULT_ZJ_0001&version=V1.0.1&model=HG261GS&HDversion=HS.V2.0&supplier=FiberHome&city=310000&applyid=1289820708&rdMD5=a8381eb16324fc69647a19aaeda7b406 HTTP/1.0\r\n"
			"User-Agent: WiFiDog 1.1 \r\n"
			"Host: apupgrade.51awifi.com \r\n"
			"\r\n");
	/*
			"GET %s?devid=%s&version=%s&model=%s&HDversion=%s&supplier=%s&city=%s&applyid=%s&rdMD5=%s HTTP/1.0\r\n", 
			"http://apupgrade.51awifi.com:80/upgrade/update.do",	// update_server->serv_path,
									// Util.c/urlRead() ...
			"DEFAULT_ZJ_0001",		// config->dev_id,
			"V1.0.1",
			"HG261GS",
			"HS.V2.0",
			"FiberHome",
			"310000",
			"1289820708",
			"a8381eb16324fc69647a19aaeda7b406");
	*/		

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

	// The correct URL returned is a download address for update file suffixed with ".bin"
	if (strstr(request, ".bin")) {
		if (retrieve_update_file(&request)) {
			debug(LOG_DEBUG, "Retrieving update file failed");
			return -1;
		}
	} else {
		debug(LOG_DEBUG, "HTTP request format maybe wrong");
		return -1;
	}

	network_traffic();

	if (do_update()) {
		debug(LOG_DEBUG, "Sysupgrade failed");
		return -1;
	}


	return 0;
}


int
retrieve_update_file(char *request)
{
	char cmd[MAX_BUF] = "wget -c -O /tmp/ctbri.bin ";
	char buf[MAX_BUF];
	char *tmp = strstr(request, "http://apupgrade.51awifi.com/upload");

	/* XXX The premiss is the url for update files is in the end of the response,
	 * if not, need to find the update url end by the suffix ".bin" */
	snprintf(buf, strlen(tmp) + 1, tmp);
	strcat(cmd, buf);
	/*
	if (execute(cmd, 0)) {
		debug(LOG_DEBUG, "Retrieving update file command failed: %s", cmd);
		return 1;
	}
	*/
	debug(LOG_DEBUG, "Retrieving update file command executed successfully: %s", cmd);

	return 0;
}

unsigned long int
network_traffic()
{
	FILE *fh;
	unsigned long int	traffic_in_old = 0;
	unsigned long int	traffic_in = 0;
	unsigned long int	traffic_in_diff = 0;
	// In release version, interval time is 300 seconds
	unsigned int 		seconds = 10;

	if (fh = fopen("/proc/net/dev", "r")) {
		while (!feof(fh)) {
			/* XXX Need reading the network interface from config file
			 * rather than assigning a static interface */
			if(fscanf(fh, "br-lan: %lu", &traffic_in_old) != 1) {
				/* Not on this line */
				while (!feof(fh) && fgetc(fh) != '\n');
			}
			else {
				/* Found it */
				debug(LOG_DEBUG, "Read old date in dev file successfully: %lu", traffic_in_old);
				break;
			}
		}
		fclose(fh);
	}
	
	sleep(seconds);
	
	if (fh = fopen("/proc/net/dev", "r")) {
		while (!feof(fh)) {
			/* XXX Need reading the network interface from config file
			 * rather than assigning a static interface */
			if(fscanf(fh, "br-lan: %lu", &traffic_in) != 1) {
				/* Not on this line */
				while (!feof(fh) && fgetc(fh) != '\n');
			}
			else {
				/* Found it */
				debug(LOG_DEBUG, "Read date in dev file successfully: %lu", traffic_in);
				break;
			}
		}
		fclose(fh);
	}

	traffic_in_diff = (traffic_in - traffic_in_old) / seconds;
	debug(LOG_DEBUG, "Network traffic now is: %lu Kbps", traffic_in_diff);
	traffic_in_diff = traffic_in_diff / 1024;
	debug(LOG_DEBUG, "Network traffic now is: %lu KB/s", traffic_in_diff);

	return traffic_in_diff;
}

int
do_update()
{
	// UPDATE_FILE
	return 0;
}























