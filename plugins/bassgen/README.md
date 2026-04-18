# bassgen port

This directory will hold the `downspout` port of `~/github/flues/lv2/bassgen`.

Near-term work:

- audit the LV2 implementation;
- extract transport, pattern, variation, and state logic into portable core code;
- map parameters and state into a DPF/VST3-friendly shape;
- add host integration after the core behavior is covered by tests.
