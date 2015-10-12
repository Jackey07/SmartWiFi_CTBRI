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
/**
  @file update.c
  @brief Auto update functions
  @author Copyright (C) 2015 CTBRI <guojia@ctbri.com.cn>
 */

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
	unsigned int		rand_time;

	while (1) {
#if DEBUG
		/* Set a timer to activate the update procedure
		 * If current time is not in the update period, delay it untile 2:00 */
		if (in_update_time_period(0)) {
			delay_to_next_day();
			debug(LOG_DEBUG, "Delay update procedure to next day");
			continue;
		}
#endif

		rand_time = random_delay_time();
		debug(LOG_DEBUG, "Update procedure will execute after random delay time %d seconds", rand_time);
#if DEBUG
		sleep(rand_time);
#endif
		if (update()) {
			debug(LOG_DEBUG, "Update failed");
			/* If update failed, try it the next day */
			delay_to_next_day();
			continue;
		} else {
			timep = time(NULL);
			debug(LOG_DEBUG, "Updated successfully in: %s", asctime(localtime(&timep)));
		}	
	}
}

/* Delay time until 2:00 next day */
int
delay_to_next_day()
{
	pthread_cond_t		cond = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t		cond_mutex = PTHREAD_MUTEX_INITIALIZER;
	struct	timespec	time_to_update;
	time_t				timep;
	struct tm			*p_tm;
	int					current_hour, interval_hour;

	timep = time(NULL);
	p_tm = localtime(&timep);
	current_hour = p_tm->tm_hour;
	/* Calculate hours need to sleep */
	interval_hour = (26 - current_hour) % 24;
	/* Convert hours to seconds */
	time_to_update.tv_sec = time(NULL) + interval_hour * 3600;
	time_to_update.tv_nsec = 0;

	/* Mutex must be locked for pthread_cond_timedwait... */
	pthread_mutex_lock(&cond_mutex);
	/* Thread safe "sleep" */
	pthread_cond_timedwait(&cond, &cond_mutex, &time_to_update);
	/* No longer needs to be locked */
	pthread_mutex_unlock(&cond_mutex);

	return 0;
}

/* Delay sending request for a random time
 * Using random function regarding the last 2 characters of MAC address as seed */
unsigned int
random_delay_time()
{
	unsigned int	delay_time = 0;
	unsigned int	seed = 0;
	
	s_config		*config = config_get_config();
	char			*mac = config->gw_mac;
	/* Point to last 2 characters */
	char			*sub_mac = mac + strlen(config->gw_mac) - 2;
	int				ctoi_1 = *sub_mac;
	int				ctoi_2 = *(sub_mac + 1);
	char			str_seed[10];
	/* Convert characters to integers */
	sprintf(str_seed, "%d%d", ctoi_1, ctoi_2);
	
	seed = atoi(str_seed);
	srand(seed);
	/* random delay time is in the range of 0 - 3600 seconds (a hour) */
	delay_time = rand() % 3600;

	return delay_time;
}

/* Check the current time or current time with delay time
 * whether in the period 2:00 - 5:00 */
int
in_update_time_period(unsigned int delay_time)
{
	time_t		timep = time(NULL);	
	struct tm	*p_tm;
	int			current_hour;
	/* If need to delay some time */
	if (delay_time) {
		timep += delay_time;
	}
	
	p_tm = localtime(&timep);
	/* When current time with delay time is more than a day
	 * make sure the time format correct */
	current_hour = (p_tm->tm_hour) % 24;
	
	if ((current_hour < 2) || (current_hour > 5)) {
		debug(LOG_DEBUG, "Current time is not in the period 2:00 - 5:00");
		return -1;
	} else {
		return 0;
	}
}

/* Main procedure of update */
int
update(void)
{
	int				sockfd;
	char			request[MAX_BUF], response[MAX_BUF];
	t_serv			*update_server = get_update_server();
	s_config		*config = config_get_config();
	unsigned int	delay_time = DELAY_TIME;
	unsigned int	times = 0;
	int				is_update_url = 0;
	char			*update_url;
	char			update_ver[VER_LENGTH];
	memset(update_ver, 0, VER_LENGTH);

	debug(LOG_DEBUG, "Entering main procedure of update");
	
	if ((sockfd = connect_update_server()) == -1) {
		return -1;
	}

	snprintf(request, sizeof(request) - 1,
			"GET %s%sdevid=%s&version=%s&model=%s&HDversion=%s&supplier=%s&city=%s&applyid=%s&rdMD5=%s HTTP/1.0\r\n"
			"User-Agent: SmartWiFi 1.1 \r\n"
			"Host: %s \r\n"
			"\r\n", 
			update_server->serv_path,
			update_server->serv_update_script_path_fragment,
			config->dev_id,				//	update_devid_Read(),
			update_ver_Read(),
			"HG261GS",
			"HS.V2.0",
			update_supplier_Read(),
			update_postcode_Read(),
			"1289820708",
			"a8381eb16324fc69647a19aaeda7b406",
			update_server->serv_hostname);

	debug(LOG_DEBUG, "HTTP Request to Server: [%s]", request);

	do {
		send_request(sockfd, &request, &response);
		debug(LOG_DEBUG, "Response from Server: [%s]", response);
		
		/* XXX The premiss is that the url for update file is in the end of the response,
		 * if not, need to find the update url ended by the suffix ".bin" */
		if (strstr(response, ".bin") == NULL) {
			times++;
			if (in_update_time_period(delay_time) == 0) {
#if DEBUG
				/* Sleep for a while then try to send request again */
				sleep(delay_time);
#endif
				continue;
			} else {
				close(sockfd);
				return -1;
			}
		} else {
			is_update_url = 1;
			break;
		}
	
	} while (times < 3);
;
	close(sockfd);

	if (times >= 3) {
		debug(LOG_DEBUG, "Trying to get update url more than 3 times");
		return -1;
	}

	/* The correct URL returned is a download address for update file suffixed with ".bin" */
	if (is_update_url) {
		update_url = strstr(response, "http://apupgrade.51awifi.com/upload");
		debug(LOG_DEBUG, "Update url is: %s", update_url);
		if (retrieve_update_file(update_url)) {
			debug(LOG_DEBUG, "Retrieving update file failed");
			return -1;
		}
	} else {
		debug(LOG_DEBUG, "HTTP request format maybe wrong");
		return -1;
	}

	if (check_network_traffic()) {
		debug(LOG_DEBUG, "Network traffic rate is too high for update procedure");
		return -1;
	}

	if (do_update()) {
		debug(LOG_DEBUG, "Update command failed");
		return -1;
	}

	get_update_ver(update_url, &update_ver);
	if (strlen(update_ver) == 0) {
		debug(LOG_DEBUG, "Getting update version failed");
		return -1;
	} else if (update_ver_Edit(update_ver)) {
		debug(LOG_DEBUG, "Failed in configuring update version to file");
		return -1;
	}

	/* TODO report to log server? */
	return 0;
}

int
send_request(int sockfd, char *request, char *response)
{
	ssize_t		numbytes = 0;
	size_t		totalbytes = 0;
	int			nfds, done = 0;
	fd_set		readfds;

	send(sockfd, request, strlen(request), 0);
	debug(LOG_DEBUG, "Reading response");

	do {
		FD_ZERO(&readfds);
		FD_SET(sockfd, &readfds);

		nfds = sockfd + 1;
		nfds = select(nfds, &readfds, NULL, NULL, NULL);

		if (nfds > 0) {
			numbytes = read(sockfd, response + totalbytes, MAX_BUF - (totalbytes + 1));
			if (numbytes < 0) {
				debug(LOG_ERR, "An error occurred while reading from update server: %s", strerror(errno));
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
			return -1;
		}
		else if (nfds < 0) {
			debug(LOG_ERR, "Error reading data via select() from update server: %s", strerror(errno));
			return -1;
		}
	} while (!done);

	debug(LOG_DEBUG, "Done reading reply, total %d bytes", totalbytes);
	response[totalbytes] = '\0';

	return totalbytes;
}

/* Retrieve update file from update server */
int
retrieve_update_file(char *request)
{
	char cmd[MAX_BUF];
/*	
	char rm_cmd[MAX_BUF];
	sprintf(rm_cmd, "rm %s", UPDATE_FILE);
	if (execute(rm_cmd, 0) == 0) {
		debug(LOG_DEBUG, "rm update file command successfully: %s", rm_cmd);
	}
*/
	sprintf(cmd, "wget -c -O %s %s", UPDATE_FILE, request);

	if (execute(cmd, 0)) {
		debug(LOG_DEBUG, "Retrieving update file command failed: %s", cmd);
		return -1;
	}

	debug(LOG_DEBUG, "Retrieving update file command executed successfully: [%s]", cmd);

	return 0;
}

/* Check network traffic, if the rate is high, check it minutes later
 * maximum check times is 3 */
unsigned long int
check_network_traffic()
{
	unsigned long int	traffic_in_old = 0;
	unsigned long int	traffic_in = 0;
	unsigned long int	traffic_in_diff = 0;
	unsigned int 		interval_time = INTERVAL_TIME;
	unsigned int		delay_time = DELAY_TIME;
	unsigned int		times = 0;

	do {
		traffic_in_old = get_network_traffic();
		sleep(interval_time);
		traffic_in = get_network_traffic();

		traffic_in_diff = (traffic_in - traffic_in_old) / interval_time;
		traffic_in_diff /= 1024;
		debug(LOG_DEBUG, "Network traffic now is: %lu KB/s", traffic_in_diff);

		if (traffic_in_diff > 10) {
			times++;
			/* If network traffic is more than 10KB/s, it means network is busy,
			 * that maybe influence update process, so we try again a few minutes later */
			sleep(delay_time);
			continue;
		} else {
			break;
		}
	} while (times < 3);

	if (times >= 3) {
		debug(LOG_DEBUG, "Geting network traffic more than 3 times, each time traffic rate > 10KBps");
		return -1;
	} else {
		return traffic_in_diff;
	}
}

/* Get received network traffic from local file "/proc/net/dev" */
unsigned long int
get_network_traffic()
{
	FILE				*fh;
	char				str_gw_if[20];
	unsigned long int	traffic_in = 0;
	s_config			*config = config_get_config();

	sprintf(str_gw_if, "%s: %%lu", config->gw_interface);

	if (fh = fopen("/proc/net/dev", "r")) {
		while (!feof(fh)) {
			/* Read the number following gw_interface, which is received network traffic */
			if(fscanf(fh, str_gw_if, &traffic_in) != 1) {
				/* Not on this line */
				while (!feof(fh) && fgetc(fh) != '\n');
			}
			else {
				/* Found it */
				debug(LOG_DEBUG, "Read the number of received packets in the file /proc/net/dev successfully: %lu", traffic_in);
				break;
			}
		}
		fclose(fh);
	}

	return traffic_in;
}

/* Execute update command */
int
do_update()
{
	char	cmd[MAX_BUF];
	pid_t	ppid, pid;
	sprintf(cmd, "opkg install --force-overwrite %s", UPDATE_FILE);

	// Maybe we should try to use system()?
	if (execute(cmd, 0)) {
		debug(LOG_DEBUG, "Update command failed: %s", cmd);
		return -1;
	}
 
	if (execute("reboot", 0)) {
		debug(LOG_DEBUG, "Reboot smartwifi command failed: %s", cmd);
		return -1;
	}

	return 0;
}

/* XXX Get update version from update url, mainly from the name of update file,
 * the premiss is that update version is included in update file name */
int
get_update_ver(char *update_url, char *update_ver)
{
	char	*ptr_p, *ptr;
	int		ver_length;

	if (update_url == NULL) {
		return -1;
	}

	/* Find the last slash with the following */
	ptr_p = update_url;
	do {
		ptr = ptr_p;
		/* Skip slash */
		ptr++;
		/* Find next string prefixed by slash */
		ptr_p = strstr(ptr, "/");
	} while (ptr_p != NULL);

	/* Remove suffix of update file, the rest is version */
	ver_length = strlen(ptr) - 4;
	strncpy(update_ver, ptr, ver_length);
	update_ver[ver_length] = '\0';

	debug(LOG_DEBUG, "Update version is: %s", update_ver);

	return 0;
}


