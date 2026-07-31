# Mnemosyne design

The reservoir contains eight phrases of at most 64 note records. Versioned text
state stores note, velocity, sixteenth-position, and duration. Processing never
allocates: input capture, recall, transformation, and active-note cleanup all
use fixed arrays. Stop and seek release generated notes and reset incomplete
capture without deleting the serialized reservoir.

The UI exposes eight reservoir slots, named operating modes and transforms, an
empty-memory instruction, capture activity, and an explicit memory-clear
action sent through the existing serialized reservoir state.
