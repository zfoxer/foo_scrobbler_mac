### Foo Scrobbler for Mac [foo_scrobbler_mac] (c) 2025 by Konstantinos Kyriakopoulos
#### Version: 0.7.3 — License: GNU GPL Version 3

### Introduction

Foo Scrobbler is a native Last.fm scrobbling plugin for foobar2000 on macOS. Submits tracks based on precise playback rules, caches scrobbles when offline, and operates silently after one-time authentication. Built using the official foobar2000 plugin API, it focuses on reliability, low overhead, and correct metadata handling. Fully open-source under GPLv3.

Supports macOS >= 11.5 on both Intel and ARM. Built with foobar2000 SDK (version: SDK-2025-03-07)


### Key Features

- **Native macOS Last.fm scrobbling**  
  Fully integrated with foobar2000 for macOS. No compatibility layers or wrapper apps.

- **Smart submission logic**  
  Scrobbles only when playback is meaningful (≥ 50% or ≥ 240 seconds).

- **Automatic offline caching**  
  If Last.fm or the network is unavailable, scrobbles are stored and submitted automatically later.

- **Accurate “Now Playing” handling**  
  Fully aligned with Last.fm API specifications.

- **Minimal user interaction**  
  Authentication required only once.

- **Lightweight and efficient**  
  Runs inside foobar2000 without performance loss.

- **Strict playback validation**  
  Prevents malformed or duplicate scrobbles.

- **Open-source (GPLv3)**  
  Transparent and extensible.

- **Built specifically for foobar2000 on macOS**  
  Not a port.

### Usage

Install the component into foobar2000 for macOS and restart the application. Open the Foo Scrobbler menu, authenticate once using your Last.fm account, and resume playback. Scrobbling runs automatically in the background with no further user intervention required.

### Licensing Notice

This project is licensed under the GNU GPLv3.

The SDK is proprietary and **not covered by the GPL license**. It remains the property of its original author (Peter Pawlowski / foobar2000).

Only the source code of the Foo Scrobbler plugin is licensed under GPLv3.

### Changelog

<pre>
0.7.3    2025-12-XX    Added option to enable/disable current scrobbling while the user remains authenticated.
0.7.0    2025-12-01    Initial release.
</pre>
