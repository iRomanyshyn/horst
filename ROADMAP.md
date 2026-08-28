# horst modernization roadmap

The goal of this fork is to keep horst small and fast while turning it into a modern Wi-Fi troubleshooting TUI rather than a deep packet-inspection tool.

## 0.1 — Resurrect horst

- [x] Add CI builds with GCC and Clang.
- [x] Add an ASan/UBSan build.
- [x] Add GNU-style long command-line options without removing existing short options.
- [x] Add `--duration` / `-T` for bounded captures.
- [x] Add `--filter-signal` for minimum RSSI filtering.
- [x] Add JSON Lines (`--output-format jsonl`) packet export.
- [x] Fix HT40 center-frequency calculation in the command-line channel selector.
- [ ] Audit compiler warnings and enable stricter warning gates.
- [ ] Add parser regression tests using saved radiotap/802.11 frames.
- [ ] Add fuzzing for radiotap and 802.11 parsing paths.
- [ ] Review the pinned libuwifi revision and define an update strategy.

## 0.2 — Modern Wi-Fi PHY

- [ ] Replace the fixed legacy/HT rate-index model with PHY metadata suitable for HT/VHT/HE/EHT.
- [ ] Decode and expose MCS, NSS, channel width and guard interval.
- [ ] Add VHT (802.11ac) and HE (802.11ax) presentation.
- [ ] Add 6 GHz band support and remove assumptions that exactly two bands exist.
- [ ] Add noise/SNR and per-antenna RSSI when supplied by radiotap/driver data.
- [ ] Rework airtime calculations for modern PHY modes and validate that utilization cannot exceed physical bounds because of accounting errors.

## 0.3 — Troubleshooting TUI

- [ ] Add an AP → client association tree.
- [ ] Add per-client and per-BSSID airtime views.
- [ ] Highlight clients that consume disproportionate airtime relative to throughput.
- [ ] Add sortable columns for RSSI, airtime, retry rate, throughput, channel and last seen.
- [ ] Add interactive text filtering/search.
- [ ] Add OUI/vendor lookup with randomized-MAC indication.
- [ ] Replace the current spectrum view with band-aware channel occupancy and overlapping-channel visualization.

## 0.4 — WLAN diagnostics

- [ ] Decode RSN information into WPA2/WPA3/SAE/OWE/Enterprise and PMF state.
- [ ] Add authentication, association, reassociation, disassociation and deauthentication event timelines.
- [ ] Detect likely roaming events and report AP transitions and observed gaps.
- [ ] Parse useful 802.11k/v/r management information where observable passively.
- [ ] Add a concise per-client health summary combining RSSI, retry rate, PHY mode and airtime.

## Later

- [ ] PCAPNG export retaining radiotap metadata for follow-up inspection in Wireshark.
- [ ] Redesign the remote protocol with explicit framing, versioning and bounds checks.
- [ ] IPv6 support for remote collectors.
- [ ] Multiple simultaneous remote viewers.
- [ ] A dedicated headless collector mode suitable for OpenWrt/Raspberry Pi probes.
