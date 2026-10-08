| scenario | runs | attacks | JamWarden detection | receiver flags only | median latency (s) | p90 latency (s) | false alarms |
|---|---|---|---|---|---|---|---|
| clean | 20 | 0 | - | - | - | - | 0 |
| jam_broadband | 20 | 20 | 100 % | 50 % | 12.0 | 15 | 0 |
| jam_cw | 20 | 20 | 100 % | 35 % | 19.0 | 24 | 0 |
| spoof_drag | 20 | 20 | 100 % | 45 % | 8.5 | 11 | 0 |
| spoof_jump | 20 | 20 | 100 % | 65 % | 2.0 | 2 | 0 |
| spoof_time | 20 | 20 | 100 % | 55 % | 2.0 | 2 | 0 |
| time_only | 20 | 20 | 100 % | 0 % | 2.0 | 2 | 0 |
| jam_then_spoof | 20 | 40 | 100 % | 45 % | 10.0 | 13 | 0 |
| obstruction | 20 | 0 | - | - | - | - | 0 |
| multipath | 20 | 0 | - | - | - | - | 0 |
| weak_jam | 20 | 20 | 50 % | 0 % | 31.5 | 47 | 0 |
| smart_spoof_slow | 20 | 20 | 100 % | 45 % | 163.5 | 171 | 0 |
| ship_clean | 20 | 0 | - | - | - | - | 0 |
| ship_spoof_jump | 20 | 20 | 100 % | 50 % | 2.0 | 2 | 0 |

Simulated time: 70.0 h - false alarms: 0 (0.0 per hour)
