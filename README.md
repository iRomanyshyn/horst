# horst — Highly Optimized Radio Scanning Tool

`horst` is a small, fast IEEE 802.11 WLAN analyzer with an ncurses interface.
It sits between simple wireless utilities and heavyweight packet analyzers: the
focus is a quick operational view of what is happening in the air rather than
deep inspection of every frame.

This repository is a modernization fork of
[br101/horst](https://github.com/br101/horst). The original project and history
remain the foundation; this fork focuses on current Linux systems, safer parsing
and build tooling, automation-friendly output, modern Wi-Fi PHY support and
practical troubleshooting workflows.

See [ROADMAP.md](ROADMAP.md) for planned work.

## What horst shows

- RSSI per station/AP.
- Channel utilization based on observed frame airtime.
- Channel/spectrum overview.
- Packet history with signal, frame type and PHY rate.
- Stations grouped by ESSID and AP association.
- Packet/byte/airtime statistics by rate and frame type.
- Filters for packet type, operating mode, MAC address and BSSID.
- Remote client/server monitoring.
- Automatic monitor-interface creation and cleanup.
- Legacy mesh/IBSS diagnostics inherited from the original project.

## New in this fork

The current modernization branch adds:

- GNU-style long command-line options while retaining the historical short
  options.
- Time-bounded captures with `--duration` / `-T`.
- Minimum-RSSI filtering with `--filter-signal`.
- JSON Lines export with `--output-format jsonl`.
- GCC, Clang, ASan and UBSan CI builds.
- Correct HT40 center-frequency handling in CLI channel selection.

Examples:

```bash
# Interactive capture
sudo horst --interface wlan0

# Run for 30 seconds
sudo horst --interface wlan0 --duration 30

# Ignore weak frames below -75 dBm
sudo horst --interface wlan0 --filter-signal -75

# Headless 15-second JSONL capture
sudo horst --interface wlan0 --quiet --duration 15 \
  --outfile scan.jsonl --output-format jsonl
```

## Requirements

On Debian/Ubuntu:

```bash
sudo apt install build-essential libncurses-dev libnl-3-dev \
  libnl-genl-3-dev pkg-config
```

Equivalent development packages are required on other Linux distributions.
The wireless interface must support monitor mode.

## Checkout and build

`libuwifi` is included as a git submodule, and it has its own submodules, so
clone recursively:

```bash
git clone --recursive https://github.com/iRomanyshyn/horst.git
cd horst
make
```

If the repository was cloned without submodules:

```bash
git submodule update --init --recursive
make
```

For verbose or debug builds:

```bash
make V=1
make DEBUG=1
```

Install under `/usr/local` by default:

```bash
sudo make install
```

`PREFIX` and `DESTDIR` are supported by the Makefile.

## Monitor mode

`horst` can create a virtual monitor interface automatically. You can also make
one manually while keeping the normal interface present:

```bash
sudo iw wlan0 interface add mon0 type monitor
sudo ip link set mon0 up
sudo horst --interface mon0
```

If the physical radio is simultaneously in use as an associated client or AP,
the driver normally cannot let `horst` freely hop channels without disrupting
that connection. For channel scanning, use a dedicated radio or place the radio
fully into monitor mode.

## Output

The historical CSV format remains the default:

```bash
sudo horst -i wlan0 -o capture.csv
```

JSON Lines is intended for scripting, `jq`, log pipelines and later survey
analysis:

```bash
sudo horst -i wlan0 -q -T 10 -o capture.jsonl --output-format jsonl
```

## Configuration

The default configuration file is `/etc/horst.conf`; use `-c` / `--config` to
select another one. See `horst.conf`, `horst.conf.5`, and `horst.8` for the
available settings and command-line options.

A MAC-to-name mapping can be loaded with `-M` / `--mac_names`. The input can be
a dnsmasq `dhcp.leases` file or simple `MAC whitespace NAME` lines.

## Remote monitoring

Start a headless server on the capture node:

```bash
sudo horst --interface wlan0 --server --quiet
```

Connect from another host:

```bash
horst --client 192.0.2.10
```

The inherited remote protocol is old, IPv4-only and unauthenticated. Treat it as
trusted-network functionality for now; redesigning it is on the roadmap. Use an
SSH tunnel when crossing an untrusted network.

## Project direction

The main modernization targets are:

1. VHT/HE/EHT-aware PHY metadata: MCS, NSS, bandwidth, GI, 6 GHz and eventually
   Wi-Fi 7 data where radiotap/driver support permits it.
2. Correct and useful airtime analysis per client, BSSID and SSID.
3. A troubleshooting-oriented TUI: AP-to-client tree, retry/airtime hot spots,
   sortable views and interactive filtering.
4. Modern security decoding: WPA2/WPA3, SAE, OWE, Enterprise and PMF.
5. Roaming and association event timelines.
6. Safer, versioned remote collectors and PCAPNG export.

The intent is to keep horst lightweight. Wireshark remains the right tool for
deep packet inspection; horst should tell you quickly where to look.

## License and upstream

Copyright © 2005–2016 Bruno Randolf and contributors, plus subsequent fork
contributors. Licensed under GPL-2.0-or-later as described by the source files
and repository license.

Original upstream: <https://github.com/br101/horst>

Original contributors include Horst Krause, Sven-Ola Tuecke, Robert Schuster,
Jonathan Guerin, David Rowe, Antoine Beaupré, Rami Refaeli, Joerg Albert,
Tuomas Räsänen and Jiantao Fu.
