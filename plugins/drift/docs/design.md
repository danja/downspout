# Drift design

All four lanes use absolute transport steps and a stateless seed hash where
possible. Walk state is reset on seek. Each lane emits at most one CC per block,
and a global events-per-second limit prevents dense grids from flooding bounded
host queues. No processing-path allocation or locking is used.
