/* horst - Highly Optimized Radio Scanning Tool
 *
 * Copyright (C) 2014-2016 Bruno Randolf (br1@einfach.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <err.h>
#include <errno.h>
#include <limits.h>

#include <uwifi/util.h>
#include <uwifi/wlan_util.h>
#include <uwifi/log.h>

#include "main.h"
#include "hutil.h"
#include "control.h"
#include "conf_options.h"

struct conf_option {
	int		option;
	const char*	name;
	int		value_required;
	const char*	default_value;
	bool		(*func)(const char* value);
};

#define CONF_OPTION_COUNT (sizeof(conf_options) / sizeof(struct conf_option))
#define LONG_OPT_BASE 1000

/* Startup configuration is applied in several passes. Keep output files closed
 * until all config-file and command-line overrides are known, so output format
 * and outfile are independent of option ordering. */
static bool parsing_initial_config;

static bool conf_quiet(__attribute__((unused)) const char* value) {
	conf.quiet = 1;
	return true;
}

#if DEBUG
static bool conf_debug(__attribute__((unused)) const char* value) {
	conf.debug = 1;
	return true;
}
#endif

static bool conf_interface(const char* value) {
	strncpy(conf.intf.ifname, value, IF_NAMESIZE);
	conf.intf.ifname[IF_NAMESIZE] = '\0';
	return true;
}

static bool conf_add_monitor(const char* value) {
	if (value != NULL && strcmp(value, "0") == 0)
		conf.add_monitor = 0;
	else {
		conf.add_monitor = 1;
	}
	return true;
}

static bool conf_outfile(const char* value) {
	if (!parsing_initial_config) {
		dumpfile_open(value);
		return true;
	}

	if (value == NULL || value[0] == '\0') {
		conf.dumpfile[0] = '\0';
		return true;
	}

	strncpy(conf.dumpfile, value, MAX_CONF_VALUE_STRLEN);
	conf.dumpfile[MAX_CONF_VALUE_STRLEN] = '\0';
	return true;
}

static bool conf_output_format(const char* value) {
	enum output_format new_format;

	if (value == NULL) {
		LOG_ERR("Output format requires a value (csv or jsonl)");
		return false;
	}

	if (strcasecmp(value, "csv") == 0)
		new_format = OUTPUT_FORMAT_CSV;
	else if (strcasecmp(value, "jsonl") == 0 || strcasecmp(value, "ndjson") == 0)
		new_format = OUTPUT_FORMAT_JSONL;
	else {
		LOG_ERR("Unknown output format '%s' (expected csv or jsonl)", value);
		return false;
	}

	/* A live control command must not append records in a different format to
	 * an already-open dump. Close it first with `outfile=` and then switch. */
	if (!parsing_initial_config && conf.dumpfile[0] != '\0' &&
	    new_format != conf.output_format) {
		LOG_ERR("Cannot change output format while an outfile is open; close it with 'outfile=' first");
		return false;
	}

	conf.output_format = new_format;
	return true;
}

static bool conf_duration(const char* value) {
	char* end = NULL;
	unsigned long duration;

	if (value == NULL) {
		LOG_ERR("Duration requires a value");
		return false;
	}

	errno = 0;
	duration = strtoul(value, &end, 10);
	if (errno != 0 || end == value || *end != '\0' || duration > UINT_MAX) {
		LOG_ERR("Invalid duration '%s'", value);
		return false;
	}
	conf.duration = (unsigned int)duration;
	return true;
}

static bool conf_filter_signal(const char* value) {
	char* end = NULL;
	long signal;

	if (value == NULL) {
		LOG_ERR("Signal threshold requires a value");
		return false;
	}

	errno = 0;
	signal = strtol(value, &end, 10);
	if (errno != 0 || end == value || *end != '\0' || signal < -200 || signal > 0) {
		LOG_ERR("Invalid signal threshold '%s' (expected -200..0 dBm)", value);
		return false;
	}
	conf.filter_signal = (int)signal;
	return true;
}

static bool conf_node_timeout(const char* value) {
	conf.node_timeout = atoi(value);
	return true;
}

static bool conf_receive_buffer(const char* value) {
	conf.recv_buffer_size = atoi(value);
	return true;
}

static bool conf_channel_set(const char* value) {
	bool ht40plus = false;
	enum uwifi_chan_width width = CHAN_WIDTH_20_NOHT;

	char* pos = strchr(value, '+');
	if (pos != NULL) {
		width = CHAN_WIDTH_40;
		ht40plus = true;
		*pos = '\0';
	} else if ((pos = strchr(value, '-')) != NULL) {
		width = CHAN_WIDTH_40;
		ht40plus = false;
		*pos = '\0';
	}

	int n = atoi(value);

	struct uwifi_chan_spec ch;
	ch.freq = wlan_chan2freq(n);
	ch.width = width;
	ch.center_freq = ch.freq;
	if (width == CHAN_WIDTH_40)
		ch.center_freq += ht40plus ? 10 : -10;

	if (conf.intf.channel_initialized)
		uwifi_channel_change(&conf.intf, &ch);
	else {
		/* We have not yet initialized the channel module, channel will be
		* changed in channel_init(). */
		conf.intf.channel_set = ch;
	}
	return true;
}

static bool conf_channel_scan(const char* value) {
	if (value != NULL && strcmp(value, "0") == 0)
		conf.intf.channel_scan = 0;
	else {
		conf.intf.channel_scan = 1;
		conf.display_view = 's'; // show spectrum view
	}
	return true;
}

/*
 * This configuration option (channel_scan_rounds=X) defines the number of
 * rounds horst scans the channel spectrum when the automatic channel scanning
 * is enabled (channel_scan=1). A scan round is considered to be complete when
 * the current channel is changed back to the initial channel. When horst goes
 * out of scan rounds, it quits.
 */
static bool conf_channel_scan_rounds(const char* value) {
	conf.intf.channel_scan_rounds = atoi(value);
	return true;
}

static bool conf_channel_dwell(const char* value) {
	conf.intf.channel_time = atoi(value) * 1000;
	return true;
}

static bool conf_channel_upper(const char* value) {
	conf.intf.channel_max = atoi(value);
	return true;
}

static bool conf_display_interval(const char* value) {
	conf.display_interval = atoi(value) * 1000;
	return true;
}

static bool conf_display_view(const char* value) {
	if (strcasecmp(value, "history") == 0 || strcasecmp(value, "hist") == 0)
		conf.display_view = 'h';
	else if (strcasecmp(value, "essid") == 0)
		conf.display_view = 'e';
	else if (strcasecmp(value, "statistics") == 0 || strcasecmp(value, "stats") == 0)
		conf.display_view = 'a';
	else if (strcasecmp(value, "spectrum") == 0 || strcasecmp(value, "spec") == 0)
		conf.display_view = 's';
	return true;
}

static bool conf_server(const char* value) {
	if (value != NULL && strcmp(value, "0") == 0)
		conf.allow_client = 0;
	else
		conf.allow_client = 1;
	return true;
}

static bool conf_client(const char* value) {
	strncpy(conf.serveraddr, value, MAX_CONF_VALUE_STRLEN);
	conf.serveraddr[MAX_CONF_VALUE_STRLEN] = '\0';
	return true;
}

static bool conf_port(const char* value) {
	conf.port = atoi(value);
	return true;
}

static bool conf_control_pipe(const char* value) {
	/*
	 * Here it's a bit difficult because -X is used for two purposes:
	 * 1) allow control pipe (-X with or without argument)
	 * 2) set the name of the control pipe (-X with argument) which can
	 *    also be used in conjuction with -x
	 * That's why we don't set a default value (as it would always allow control)
	 * and especially handle the default name here and in control_send_command()
	 */
	if (value != NULL)
		strncpy(conf.control_pipe, value, MAX_CONF_VALUE_STRLEN);
	else
		strncpy(conf.control_pipe, DEFAULT_CONTROL_PIPE, MAX_CONF_VALUE_STRLEN);
	conf.control_pipe[MAX_CONF_VALUE_STRLEN] = '\0';
	conf.allow_control = 1;
	return true;
}

static bool conf_filter_mac(const char* value) {
	static int n;
	if (n >= MAX_FILTERMAC) {
		LOG_ERR("Can only handle %d MAC filters", MAX_FILTERMAC);
		return false;
	}

	conf.do_macfilter = 1;
	convert_string_to_mac(value, conf.filtermac[n]);
	conf.filtermac_enabled[n] = 1;
	n++;
	return true;
}

static bool conf_filter_bssid(const char* value) {
	convert_string_to_mac(value, conf.filterbssid);
	return true;
}

static bool conf_filter_mode(const char* value) {
	if (conf.filter_mode == WLAN_MODE_ALL)
		conf.filter_mode = 0;
	if (strcmp(value, "ALL") == 0)
		conf.filter_mode = WLAN_MODE_ALL;
	else if (strcmp(value, "AP") == 0)
		conf.filter_mode |= WLAN_MODE_AP;
	else if (strcmp(value, "STA") == 0)
		conf.filter_mode |= WLAN_MODE_STA;
	else if (strcmp(value, "ADH") == 0 || strcmp(value, "IBSS") == 0)
		conf.filter_mode |= WLAN_MODE_IBSS;
	else if (strcmp(value, "WDS") == 0)
		conf.filter_mode |= WLAN_MODE_4ADDR;
	else if (strcmp(value, "UNKNOWN") == 0)
		conf.filter_mode |= WLAN_MODE_UNKNOWN;
	return true;
}

static bool conf_filter_pkt(const char* value) {
	int t, i;

	if (conf.filter_pkt == PKT_TYPE_ALL) {
		conf.filter_pkt = 0;
		conf.filter_badfcs = 0;
		conf.filter_stype[WLAN_FRAME_TYPE_MGMT] = 0;
		conf.filter_stype[WLAN_FRAME_TYPE_CTRL] = 0;
		conf.filter_stype[WLAN_FRAME_TYPE_DATA] = 0;
	}
	if (strcmp(value, "ALL") == 0) {
		conf.filter_pkt = PKT_TYPE_ALL;
		conf.filter_badfcs = 1;
		conf.filter_stype[WLAN_FRAME_TYPE_MGMT] = 0xffff;
		conf.filter_stype[WLAN_FRAME_TYPE_CTRL] = 0xffff;
		conf.filter_stype[WLAN_FRAME_TYPE_DATA] = 0xffff;
	}
	else if (strcmp(value, "BADFCS") == 0)
		conf.filter_badfcs = 1;
	else if (strcmp(value, "CTRL") == 0 || strcmp(value, "CONTROL") == 0)
		conf.filter_stype[WLAN_FRAME_TYPE_CTRL] = 0xffff;
	else if (strcmp(value, "MGMT") == 0 || strcmp(value, "MANAGEMENT") == 0)
		conf.filter_stype[WLAN_FRAME_TYPE_MGMT] = 0xffff;
	else if (strcmp(value, "DATA") == 0)
		conf.filter_stype[WLAN_FRAME_TYPE_DATA] = 0xffff;
	else if (strcmp(value, "ARP") == 0)
		conf.filter_pkt |= PKT_TYPE_ARP;
	else if (strcmp(value, "IP") == 0)
		conf.filter_pkt |= PKT_TYPE_IP;
	else if (strcmp(value, "ICMP") == 0)
		conf.filter_pkt |= PKT_TYPE_ICMP;
	else if (strcmp(value, "UDP") == 0)
		conf.filter_pkt |= PKT_TYPE_UDP;
	else if (strcmp(value, "TCP") == 0)
		conf.filter_pkt |= PKT_TYPE_TCP;
	else if (strcmp(value, "OLSR") == 0)
		conf.filter_pkt |= PKT_TYPE_OLSR;
	else if (strcmp(value, "BATMAN") == 0)
		conf.filter_pkt |= PKT_TYPE_BATMAN;
	else if (strcmp(value, "MESHZ") == 0)
		conf.filter_pkt |= PKT_TYPE_MESHZ;

	for (t = 0; t < WLAN_NUM_TYPES; t++) {
		for (i = 0; i < WLAN_NUM_STYPES; i++) {
			if (strcasecmp(stype_names[t][i].name, value) == 0) {
				conf.filter_stype[t] |= BIT(i);
				return true;
			}
		}
	}
	return true;
}

static bool conf_mac_names(const char* value) {
	if (value != NULL)
		strncpy(conf.mac_name_file, value, MAX_CONF_VALUE_STRLEN);
	else
		strncpy(conf.mac_name_file, DEFAULT_MAC_NAME_FILE, MAX_CONF_VALUE_STRLEN);
	conf.mac_name_file[MAX_CONF_VALUE_STRLEN] = '\0';
	conf.mac_name_lookup = 1;
	return true;
}

static struct conf_option conf_options[] = {
	/* C , NAME        VALUE REQUIRED, DEFAULT	CALLBACK */
	{ 'q', "quiet",			0, NULL,	conf_quiet },		// NOT dynamic
#if DEBUG
	{ 'D', "debug",			0, NULL,	conf_debug },		// NOT dynamic
#endif
	{ 'i', "interface",			1, "wlan0",	conf_interface },	// NOT dynamic
	{ 'a', "add_monitor",		0, NULL,	conf_add_monitor },
	{ 'd', "display_interval",	1, "100",	conf_display_interval },
	{ 'V', "display_view",		1, NULL,	conf_display_view },
	{ 'o', "outfile",			1, NULL,	conf_outfile },
	{  0 , "output-format",		1, "csv",	conf_output_format },
	{ 'T', "duration",			1, "0",		conf_duration },
	{ 't', "node_timeout",		1, "60",	conf_node_timeout },
	{ 'b', "receive_buffer",		1, NULL,	conf_receive_buffer },	// NOT dynamic
	{ 'C', "channel",			1, NULL,	conf_channel_set },
	{ 's', "channel_scan",		0, NULL,	conf_channel_scan },
	{  0 , "channel_scan_rounds",	1, "-1",	conf_channel_scan_rounds },
	{  0 , "channel_dwell",		1, "250",	conf_channel_dwell },
	{ 'u', "channel_upper",		1, NULL,	conf_channel_upper },
	{ 'N', "server",			0, NULL,	conf_server },		// NOT dynamic
	{ 'n', "client",			1, NULL,	conf_client },		// NOT dynamic
	{ 'p', "port",			1, "4444",	conf_port },		// NOT dynamic
	{ 'X', "control_pipe",		2, NULL,	conf_control_pipe },	// NOT dynamic
	{ 'e', "filter_mac",			1, NULL,	conf_filter_mac },
	{ 'B', "filter_bssid",		1, NULL,	conf_filter_bssid },
	{ 'm', "filter_mode",			1, "ALL",	conf_filter_mode },
	{ 'f', "filter_packet",		1, "ALL",	conf_filter_pkt },
	{  0 , "filter-signal",		1, "0",		conf_filter_signal },
	{ 'M', "mac_names",			2, NULL,	conf_mac_names },
};

/*
 * More possible config options:
 *
 * main view:
 *	sort nodes by: signal, time, bssid, channel
 * spec view:
 *	show nodes or bars
 */


/*
 * This handles command line options from getopt as well as options from the config file
 * In the first case 'c' is non-zero and name is NULL
 * In the second case 'c' is 0 and name is set
 * Value may be null in all cases
 */
bool config_handle_option(int c, const char* name, const char* value)
{
	unsigned int i;
	char* end;

	for (i=0; i < CONF_OPTION_COUNT; i++) {
		if (((c != 0 && conf_options[i].option == c) ||
		    (name != NULL && strcmp(conf_options[i].name, name) == 0)) &&
		     conf_options[i].func != NULL) {
			if (!conf.quiet) {
				if (value != NULL)
					LOG_INF("Set '%s' = '%s'", conf_options[i].name, value);
				else
					LOG_INF("Set '%s'", conf_options[i].name);
			}
			if (value != NULL) {
				/* split list values and call function multiple times */
				while ((end = strchr(value, ',')) != NULL) {
					*end = '\0';
					if (!conf_options[i].func(value))
						return false;
					value = end + 1;
				}
			}
			/* call function */
			return conf_options[i].func(value);
		}
	}
	if (name != NULL)
		LOG_INF("Ignoring unknown config option '%s' = '%s'", name, value);
	return false;
}

static void config_read_file(const char* filename)
{
	FILE* fp ;
	char line[255];
	char name[MAX_CONF_NAME_STRLEN + 1];
	char value[MAX_CONF_VALUE_STRLEN + 1];
	int n;
	int linenum = 0;

	if ((fp = fopen(filename, "r")) == NULL) {
		LOG_ERR("Could not open config file '%s'", filename);
		return;
	}

	while (fgets(line, sizeof(line), fp) != NULL) {
		++linenum;
		if (line[0] == '#' ) // comment
			continue;

		// Note: 200 below has to match MAX_CONF_VALUE_STRLEN
		// Note: 32 below has to match MAX_CONF_NAME_STRLEN
		n = sscanf(line, " %32[^= \n] = %200[^ \n]", name, value);
		if (n < 0) { // empty line
			continue;
		} else if (n == 0) {
			LOG_ERR("Config file has garbage on line %d, "
				 "ignoring the line.", linenum);
			continue;
		} else if (n == 1) { // no value
			config_handle_option(0, name, NULL);
		} else {
			config_handle_option(0, name, value);
		}
	}

	fclose(fp);
}

static void config_apply_defaults(void)
{
	unsigned int i;
	for (i=0; i < CONF_OPTION_COUNT; i++) {
		if (conf_options[i].default_value != NULL) {
			conf_options[i].func(conf_options[i].default_value);
		}
	}
}

static char* config_get_getopt_string(char* buf, size_t maxlen, const char* add)
{
	unsigned int pos = 0;
	unsigned int i;
	maxlen = maxlen - 1; // we use it as string index

	for (i=0; i < CONF_OPTION_COUNT && pos < maxlen; i++) {
		if (conf_options[i].option != 0 && pos < maxlen) {
			buf[pos++] = conf_options[i].option;
			if (conf_options[i].value_required && pos < maxlen) {
				buf[pos++] = ':';
			}
			if (conf_options[i].value_required == 2 && pos < maxlen) {
				buf[pos++] = ':';
			}
		}
	}
	buf[pos] = '\0';

	if (add != NULL) {
		if (pos < maxlen && (maxlen - pos) >= strlen(add))
			strncat(buf, add, (maxlen - pos));
		else {
			LOG_ERR("Not enough space for getopt string!");
			exit(1);
		}
	}

	return buf;
}

static void config_get_long_options(struct option* opts, size_t maxopts)
{
	unsigned int i;
	unsigned int pos = 0;

	if (maxopts < CONF_OPTION_COUNT + 5)
		errx(1, "Not enough space for long options");

	for (i = 0; i < CONF_OPTION_COUNT; i++) {
		opts[pos].name = conf_options[i].name;
		opts[pos].has_arg = conf_options[i].value_required == 0 ? no_argument :
				     conf_options[i].value_required == 1 ? required_argument : optional_argument;
		opts[pos].flag = NULL;
		opts[pos].val = conf_options[i].option != 0 ? conf_options[i].option : LONG_OPT_BASE + i;
		pos++;
	}

	opts[pos++] = (struct option){ "help", no_argument, NULL, 'h' };
	opts[pos++] = (struct option){ "version", no_argument, NULL, 'v' };
	opts[pos++] = (struct option){ "config", required_argument, NULL, 'c' };
	opts[pos++] = (struct option){ "command", required_argument, NULL, 'x' };
	opts[pos] = (struct option){ 0, 0, 0, 0 };
}

static bool config_handle_parsed_option(int c, const char* value)
{
	unsigned int idx;

	if (c >= LONG_OPT_BASE) {
		idx = c - LONG_OPT_BASE;
		if (idx >= CONF_OPTION_COUNT)
			return false;
		return config_handle_option(0, conf_options[idx].name, value);
	}

	return config_handle_option(c, NULL, value);
}

static void print_usage(const char* name)
{
	printf("\nUsage: %s [options]\n\n"
		"General Options: Description (default value)\n"
		"  -v, --version\tShow version\n"
		"  -h, --help\t\tHelp\n"
		"  -q, --quiet\t\tQuiet, no output\n"
#if DEBUG
		"  -D, --debug\t\tShow lots of debug output, no UI\n"
#endif
		"  -a, --add_monitor\tAlways add virtual monitor interface\n"
		"  -c, --config <file>\tConfig file (" CONFIG_FILE ")\n"
		"  -C, --channel <chan>\tSet initial channel\n"
		"  -i, --interface <intf>\tInterface name (wlan0)\n"
		"  -T, --duration <sec>\tStop automatically after N seconds (0 = unlimited)\n"
		"  -t, --node_timeout <sec>\tNode timeout in seconds (60)\n"
		"  -d, --display_interval <ms>\tDisplay update interval in ms (100)\n"
		"  -V, --display_view <view>\tDisplay view: history|essid|statistics|spectrum\n"
		"  -b, --receive_buffer <bytes>\tReceive buffer size in bytes (not set)\n"
		"  -M[filename], --mac_names[=filename]\tMAC address to host name mapping (/tmp/dhcp.leases)\n"

		"\nFeature Options:\n"
		"  -s, --channel_scan\t(Poor man's) spectrum analyzer mode\n"
		"  -u, --channel_upper <chan>\tUpper channel limit\n\n"

		"  -N, --server\t\tAllow network connection, server mode (off)\n"
		"  -n, --client <IP>\tConnect to server with <IP>, client mode (off)\n"
		"  -p, --port <port>\tPort number of server (4444)\n\n"

		"  -o, --outfile <filename>\tWrite packet info into filename\n"
		"      --output-format <csv|jsonl>\tOutput format (csv)\n\n"

		"  -X[name], --control_pipe[=name]\tAllow control socket (/tmp/horst)\n"
		"  -x, --command <command>\tSend control command\n"

		"\nFilter Options:\n"
		" Filters are generally 'positive' or 'inclusive' which means you define\n"
		" what you want to see, and everything else is getting filtered out.\n"
		" If a filter is not set it is inactive and nothing is filtered.\n"
		" Most filter options can be specified multiple times and will be combined\n"
		"  -e, --filter_mac <MAC>\tSource MAC addresses, up to 9 times\n"
		"  -f, --filter_packet <PKT_NAME>\tFilter packet types, multiple\n"
		"  -m, --filter_mode <MODE>\tOperating mode: AP|STA|ADH|PRB|WDS|UNKNOWN, multiple\n"
		"  -B, --filter_bssid <MAC>\tBSSID, only one\n"
		"      --filter-signal <dBm>\tMinimum RSSI to include, e.g. -75 (0 = disabled)\n"
		"\n",
		name);
}

void config_parse_file_and_cmdline(int argc, char** argv)
{
	char getopt_str[CONF_OPTION_COUNT * 2 + 10];
	struct option long_options[CONF_OPTION_COUNT + 5];
	char* conf_filename = CONFIG_FILE;
	int c;

	config_get_getopt_string(getopt_str, sizeof(getopt_str), "hvc:x:");
	config_get_long_options(long_options, sizeof(long_options) / sizeof(long_options[0]));

	parsing_initial_config = true;

	/* first: apply default values */
	config_apply_defaults();

	/*
	 * then: handle command line options which are not
	 * configuration options ("hc:")
	 */
	while ((c = getopt_long(argc, argv, getopt_str, long_options, NULL)) > 0) {
		switch (c) {
		case 'c':
			LOG_INF("Using config file '%s'", optarg);
			conf_filename = optarg;
			break;
		case 'v':
			printf("%s using libuwifi %s\n", VERSION, UWIFI_VERSION);
			exit(0);
		case 'h':
			print_usage(argv[0]);
			exit(0);
		case '?':
			print_usage(argv[0]);
			exit(2);
		}
	}

	/* read config file */
	config_read_file(conf_filename);

	/*
	 * get command line options which are configuration, to let them
	 * override or add to the config file options
	 */
	optind = 1;
	while ((c = getopt_long(argc, argv, getopt_str, long_options, NULL)) > 0) {
		switch (c) {
		case 'c':
		case 'v':
		case 'h':
		case 'x':
		case '?':
			break;
		default:
			if (!config_handle_parsed_option(c, optarg))
				errx(2, "Invalid command line option value");
			break;
		}
	}

	parsing_initial_config = false;
	if (conf.dumpfile[0] != '\0')
		dumpfile_open(conf.dumpfile);

	/*
	 * and finally get command line options ("commands") which depend
	 * on config options ("x:")
	 */
	optind = 1;
	while ((c = getopt_long(argc, argv, getopt_str, long_options, NULL)) > 0) {
		switch (c) {
		case 'x':
			control_send_command(optarg);
			exit(0);
		}
	}
}
