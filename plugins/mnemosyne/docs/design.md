# Mnemosyne design

The reservoir contains eight phrases of at most 64 note records. Versioned text
state stores note, velocity, sixteenth-position, and duration. Processing never
allocates: input capture, recall, transformation, and active-note cleanup all
use fixed arrays. Stop and seek release generated notes and reset incomplete
capture without deleting the serialized reservoir.
