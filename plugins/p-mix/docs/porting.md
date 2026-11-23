# p-mix porting notes

Source plugin: `~/github/flues/lv2/p-mix`

Notable source concerns to preserve:

- audio gating across up to eight channels
- inputs 3-4 are Oppose's sidechain: declare the "Sidechain" port group and
  `kAudioPortIsSidechain` so hosts expose an aux bus, not a second main input
- bar-boundary state decisions
- fade and cut transition behavior
- saved/restored parameter state
- predictable behavior when transport data is missing or stopped
