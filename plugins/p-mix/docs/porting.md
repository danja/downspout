# p-mix porting notes

Source plugin: `~/github/flues/lv2/p-mix`

Notable source concerns to preserve:

- audio gating across up to eight channels
- bar-boundary state decisions
- fade and cut transition behavior
- saved/restored parameter state
- predictable behavior when transport data is missing or stopped
