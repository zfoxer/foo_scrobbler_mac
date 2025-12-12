### Foo Scrobbler for Mac [foo_scrobbler_mac] (c) 2025 by Konstantinos Kyriakopoulos
#### Version: 0.7.5 — Released under GNU GPL V3

### Intro

Foo Scrobbler is a native Last.fm scrobbling plugin for foobar2000 on macOS. Submits tracks based on precise playback rules, caches scrobbles when offline, and operates silently after one-time authentication. Built using the official foobar2000 plugin API, it focuses on reliability, low overhead, and correct metadata handling. Fully open-source under GPLv3.

Supports macOS ≥ 11.5 on both Intel and ARM. Developed with foobar2000 SDK-2025-03-07.  


### Key Features

- **Native macOS Last.fm scrobbling**  
  Fully integrated with foobar2000 for macOS. No compatibility layers or wrapper apps.

- **Smart submission logic**  
  Scrobbles only when playback is meaningful (e.g., ≥ 50% or ≥ 240 seconds).

- **Automatic offline caching**  
  If Last.fm or the network is unavailable, scrobbles are stored and submitted automatically later.

- **Accurate “Now Playing” handling**  
  Fully aligned with Last.fm Scrobbling 2.0 API specifications.

- **Minimal user interaction**  
  Authentication required only once.

- **Lightweight and efficient**  
  Runs inside foobar2000 without performance loss. Not relying on third-party dependencies.

- **Strict playback validation**  
  Prevents malformed or duplicate scrobbles.

- **Open-source (GPLv3)**  
  Transparent and extensible.

- **Built specifically for foobar2000 on macOS**  
  Not a port.

### Usage

Install the component **foo_scrobbler_mac.fb2k-component** from within foobar2000 for macOS by pointing to it from the components pane.  

Authentication requires only an active Last.fm account. Users grant access once through the Last.fm website with their account, after which Foo Scrobbler runs quietly in the background and submits track information automatically. If authentication is cleared from the menu, the same user —or a different one— must grant access again through browser redirection to the Last.fm website. Foo Scrobbler adds a simple, convenient and non-intrusive last entry under Playback in the menu bar.  


### Licensing Notice

This project is licensed under the GNU GPLv3.

The SDK is proprietary and **not covered by the GPL license**. It remains the property of its original author (Peter Pawlowski / foobar2000).

Only the source code of the Foo Scrobbler plugin is licensed under GPLv3.

### Changelog

<pre>
0.7.5    2025-12-13    Not considering candidate scrobbles with garbage tag entries.
                       Added linear back-off retry strategy per scrobble for the queue.
                       Improved internal design.
0.7.3    2025-12-07    Improved management of the communication to Last.fm.
                       Improved internal timing system according to specifications.
                       When track tags change during playback, scrobbling will detect them and use the updated info.
                       Seeking across the submission mark with the slider (e.g., 50%) doesn’t cheat the scrobble.
                       Added option to enable/disable scrobbling while the user remains authenticated.
0.7.0    2025-12-01    Initial release.
</pre>

