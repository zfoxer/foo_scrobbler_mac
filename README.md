### Foo Scrobbler for Mac
#### Version: 1.0.0 — foo_scrobbler_mac — Released under GNU GPLv3
#### © 2025-2026 by Konstantinos Kyriakopoulos

#### See the detailed [Installation Guide](https://github.com/zfoxer/foo_scrobbler_mac/wiki/Installation) and [Last.fm Authentication Guide](https://github.com/zfoxer/foo_scrobbler_mac/wiki/LFM_Auth).

### Intro

Foo Scrobbler (foo_scrobbler_mac) is a native Last.fm scrobbling plugin for foobar2000 on macOS. Submits tracks based on precise playback rules, caches scrobbles when offline, and operates silently after one-time authentication. Built using the official foobar2000 plugin API, it focuses on reliability, low overhead, and correct metadata handling. Fully open-source under GPLv3.

Supports macOS ≥ 11.5 on both Intel and ARM.  


### Key Features

- **Native macOS Last.fm scrobbling**  
  Fully integrated with foobar2000 for macOS. No compatibility layers or wrapper apps.

- **Rule-based submission logic**  
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

Install **foo_scrobbler_mac.fb2k-component** from within foobar2000 by pointing to it (that is, add via '+') from the components section.  

Authentication requires only an active Last.fm account. Users grant access once through the Last.fm website with their account, after which Foo Scrobbler runs quietly in the background and submits track information automatically. If authentication is cleared from the menu, the same user —or a different one— must grant access again through browser redirection to the Last.fm website. Foo Scrobbler adds a simple, convenient and non-intrusive last entry under Playback in the menu bar.  More options are located in Preferences → Advanced → Tools → Foo Scrobbler.


### Licensing Notice

This project is licensed under the GNU GPLv3.

The SDK is proprietary and **not covered by the GPL license**. It remains the property of its original author (Peter Pawlowski / foobar2000).

Only the source code of the Foo Scrobbler plugin is licensed under GPLv3.

### Changelog

<pre>
1.0.0    2026-02-08    Minor modifications to logging.

0.9.9    2026-01-25    Supporting dynamic sources like radio streams, etc.

0.9.7    2026-01-08    Improved Last.fm web client error handling.

0.9.6    2026-01-04    If a different user authenticates, the local scrobble cache is cleared.
                       Controlling the submission rate of the local scrobble cache to meet Last.fm requirements.
                       Fixed bug when authentication was not completed the first time and fbar restart was required.
                       Refactored the internal thread system.

0.9.5    2025-12-24    Introduced new configuration fields in Preferences → Advanced → Tools → Foo Scrobbler.
                       Added option to only scrobble tracks from the media library.
                       Added option to set the console info level: none, basic or debug.
                       Added option to disable NowPlaying notifications.
                       
0.7.7    2025-12-15    Fixed bug related to the behaviour while the user gets unauthenticated.
                       Fixed policy issues related to disabling scrobbling.

0.7.6    2025-12-14    Removed rule about the seekbar moves at first half of track which were cancelling the scrobble.
                       Fixed linear queue policy.

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

