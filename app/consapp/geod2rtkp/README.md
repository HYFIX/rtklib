# geod2rtkp

`geod2rtkp` replays GEOD-wrapped RTCM logs through the RTK engine without
loading the complete observation history into memory.

The first input is the rover, the second is the base, and any remaining inputs
are navigation/correction streams. The tool keeps one GEOD record per input,
selects the lowest `$GEOD` UTC millisecond timestamp, decodes that payload, and
immediately applies the resulting RTCM message. Equal timestamps retain command
line input order.

One rover epoch may be held briefly for a same-time base epoch. Memory usage is
therefore bounded by decoder/filter state, one record per file, and one rover
and base observation epoch rather than total log duration.

Example:

```text
geod2rtkp -k rtk.conf -r X Y Z -o solution.pos rover.log base.log nav.log
```

Frequency selection accepts `-f 1` through `-f 6`; `-f 7` selects the alternate
two-frequency L1+L5 plan. Configuration files use the same `pos1-frequency`
values as `rnx2rtkp`.
