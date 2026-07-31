# Conductor design

The portable core maintains a five-section weighted graph and reconstructs it
deterministically on seek. A section transition releases the previous section
note before sending the new note and five CC commands. Schedule reconstruction
is capped at 4096 sections and processing uses fixed event storage.

The UI presents the five-section form as a timeline, disables section weights
in fixed mode, and keeps MIDI note/CC assignments in a separate advanced
routing panel.
