/* horst - Highly Optimized Radio Scanning Tool
 *
 * Copyright (C) 2005-2016 Bruno Randolf (br1@einfach.org)
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

/******************* FILTER *******************/

#include <stdlib.h>
#include <string.h>

#include <uwifi/wlan80211.h>
#include <uwifi/wlan_util.h>

#include "display.h"
#include "main.h"
#include "hutil.h"

#define FILTER_WIN_WIDTH 86
#define FILTER_WIN_HEIGHT 22

#define PKT_COL 2
#define MAC_COL 23
#define MODE_COL 52

#define FIRST_ROW 2
#define SECOND_ROW 10
#define THIRD_ROW 14

#define CHECKED(x) ((x) ? 'X' : ' ')

static void filter_win_update(WINDOW *win)
{
	int i, l;

	werase(win);
	wattron(win, WHITE);
	box(win, 0, 0);
	print_centered(win, 0, FILTER_WIN_WIDTH, " Filter ");

	l = FIRST_ROW;
	wattron(win, A_BOLD);
	mvwprintw(win, l++, PKT_COL, "Packet Types");
	wattroff(win, A_BOLD);
	mvwprintw(win, l++, PKT_COL, "a: [%c] ALL", CHECKED(conf.filter_stype[WLAN_FRAME_TYPE_MGMT] == 0xffff &&
							conf.filter_stype[WLAN_FRAME_TYPE_CTRL] == 0xffff &&
							conf.filter_stype[WLAN_FRAME_TYPE_DATA] == 0xffff &&
							conf.filter_pkt == PKT_TYPE_ALL));
	mvwprintw(win, l++, PKT_COL, "m: [%c] MGMT", CHECKED(conf.filter_stype[WLAN_FRAME_TYPE_MGMT] == 0xffff));
	mvwprintw(win, l++, PKT_COL, "c: [%c] CTRL", CHECKED(conf.filter_stype[WLAN_FRAME_TYPE_CTRL] == 0xffff));
	mvwprintw(win, l++, PKT_COL, "d: [%c] DATA", CHECKED(conf.filter_stype[WLAN_FRAME_TYPE_DATA] == 0xffff));
	mvwprintw(win, l++, PKT_COL, "b: [%c] BADFCS", CHECKED(conf.filter_badfcs));

	l = SECOND_ROW;
	wattron(win, A_BOLD);
	mvwprintw(win, l++, PKT_COL, "Higher Protocols");
	wattroff(win, A_BOLD);
	mvwprintw(win, l++, PKT_COL, "i: [%c] IP", CHECKED(conf.filter_pkt & PKT_TYPE_IP));
	mvwprintw(win, l++, PKT_COL, "u: [%c] UDP", CHECKED(conf.filter_pkt & PKT_TYPE_UDP));
	mvwprintw(win, l++, PKT_COL, "t: [%c] TCP", CHECKED(conf.filter_pkt & PKT_TYPE_TCP));
	mvwprintw(win, l++, PKT_COL, "p: [%c] ICMP", CHECKED(conf.filter_pkt & PKT_TYPE_ICMP));
	mvwprintw(win, l++, PKT_COL, "r: [%c] ARP", CHECKED(conf.filter_pkt & PKT_TYPE_ARP));
	mvwprintw(win, l++, PKT_COL, "o: [%c] OLSR", CHECKED(conf.filter_pkt & PKT_TYPE_OLSR));
	mvwprintw(win, l++, PKT_COL, "g: [%c] BATMAN", CHECKED(conf.filter_pkt & PKT_TYPE_BATMAN));
	mvwprintw(win, l++, PKT_COL, "z: [%c] MESHZ", CHECKED(conf.filter_pkt & PKT_TYPE_MESHZ));

	l = FIRST_ROW;
	wattron(win, A_BOLD);
	mvwprintw(win, l++, MAC_COL, "MAC Addresses");
	wattroff(win, A_BOLD);
	for (i = 0; i < MAX_FILTERMAC; i++) {
		mvwprintw(win, l++, MAC_COL, "%d: [%c] " MAC_FMT, i+1,
			  CHECKED(conf.filtermac_enabled[i]),
			  MAC_PAR(conf.filtermac[i]));
	}

	l = THIRD_ROW;
	wattron(win, A_BOLD);
	mvwprintw(win, l++, MODE_COL, "BSSID");
	wattroff(win, A_BOLD);
	mvwprintw(win, l++, MODE_COL, "_: [%c] " MAC_FMT,
		CHECKED(MAC_NOT_EMPTY(conf.filterbssid)), MAC_PAR(conf.filterbssid));

	l++;

	wattron(win, A_BOLD);
	mvwprintw(win, l++, MODE_COL, "Mode");
	wattroff(win, A_BOLD);
	mvwprintw(win, l++, MODE_COL, "!: [%c] Access Point", CHECKED(conf.filter_mode & WLAN_MODE_AP));
	mvwprintw(win, l++, MODE_COL, "@: [%c] Station", CHECKED(conf.filter_mode & WLAN_MODE_STA));
	mvwprintw(win, l++, MODE_COL, "#: [%c] IBSS (Ad-hoc)", CHECKED(conf.filter_mode & WLAN_MODE_IBSS));
	mvwprintw(win, l++, MODE_COL, "%%: [%c] WDS/4ADDR", CHECKED(conf.filter_mode & WLAN_MODE_4ADDR));
	mvwprintw(win, l++, MODE_COL, "^: [%c] Unknown", CHECKED(conf.filter_mode & WLAN_MODE_UNKNOWN));

	wattroff(win, WHITE);
	print_centered(win, ++l, FILTER_WIN_WIDTH, "[ Press key or ENTER ]");

	wrefresh(win);
}

bool filter_input(WINDOW *win, int c)
{
	int i;

	switch (c) {
	case 'a': case 'A':
		if (conf.filter_stype[WLAN_FRAME_TYPE_MGMT] == 0xffff &&
		    conf.filter_stype[WLAN_FRAME_TYPE_CTRL] == 0xffff &&
		    conf.filter_stype[WLAN_FRAME_TYPE_DATA] == 0xffff &&
		    conf.filter_pkt == PKT_TYPE_ALL) {
			conf.filter_stype[WLAN_FRAME_TYPE_MGMT] = 0;
			conf.filter_stype[WLAN_FRAME_TYPE_CTRL] = 0;
			conf.filter_stype[WLAN_FRAME_TYPE_DATA] = 0;
			conf.filter_pkt = 0;
		} else {
			conf.filter_stype[WLAN_FRAME_TYPE_MGMT] = 0xffff;
			conf.filter_stype[WLAN_FRAME_TYPE_CTRL] = 0xffff;
			conf.filter_stype[WLAN_FRAME_TYPE_DATA] = 0xffff;
			conf.filter_pkt = PKT_TYPE_ALL;
		}
		break;
	case 'm': case 'M':
		conf.filter_stype[WLAN_FRAME_TYPE_MGMT] =
			conf.filter_stype[WLAN_FRAME_TYPE_MGMT] == 0xffff ? 0 : 0xffff;
		break;
	case 'c': case 'C':
		conf.filter_stype[WLAN_FRAME_TYPE_CTRL] =
			conf.filter_stype[WLAN_FRAME_TYPE_CTRL] == 0xffff ? 0 : 0xffff;
		break;
	case 'd': case 'D':
		conf.filter_stype[WLAN_FRAME_TYPE_DATA] =
			conf.filter_stype[WLAN_FRAME_TYPE_DATA] == 0xffff ? 0 : 0xffff;
		break;
	case 'b': case 'B':
		conf.filter_badfcs = !conf.filter_badfcs;
		break;
	case 'i': case 'I': conf.filter_pkt ^= PKT_TYPE_IP; break;
	case 'u': case 'U': conf.filter_pkt ^= PKT_TYPE_UDP; break;
	case 't': case 'T': conf.filter_pkt ^= PKT_TYPE_TCP; break;
	case 'p': case 'P': conf.filter_pkt ^= PKT_TYPE_ICMP; break;
	case 'r': case 'R': conf.filter_pkt ^= PKT_TYPE_ARP; break;
	case 'o': case 'O': conf.filter_pkt ^= PKT_TYPE_OLSR; break;
	case 'g': case 'G': conf.filter_pkt ^= PKT_TYPE_BATMAN; break;
	case 'z': case 'Z': conf.filter_pkt ^= PKT_TYPE_MESHZ; break;
	case '!': conf.filter_mode ^= WLAN_MODE_AP; break;
	case '@': conf.filter_mode ^= WLAN_MODE_STA; break;
	case '#': conf.filter_mode ^= WLAN_MODE_IBSS; break;
	case '%': conf.filter_mode ^= WLAN_MODE_4ADDR; break;
	case '^': conf.filter_mode ^= WLAN_MODE_UNKNOWN; break;
	case '_':
		if (MAC_NOT_EMPTY(conf.filterbssid))
			memset(conf.filterbssid, 0, WLAN_MAC_LEN);
		else
			memcpy(conf.filterbssid, conf.intf.filterbssid, WLAN_MAC_LEN);
		break;
	case '1': case '2': case '3': case '4': case '5':
	case '6': case '7': case '8': case '9':
		i = c - '1';
		conf.filtermac_enabled[i] = !conf.filtermac_enabled[i];
		break;
	case '\r': case KEY_ENTER:
		return false;
	default:
		return true;
	}

	filter_win_update(win);
	net_send_filter_config();
	return true;
}

void init_filter_win(WINDOW *win)
{
	filter_win_update(win);
}
