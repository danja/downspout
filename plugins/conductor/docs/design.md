# Conductor design

The portable core maintains a five-section weighted graph and reconstructs it
deterministically on seek. A section transition releases the previous section
note before sending the new note and five CC commands. Schedule reconstruction
is capped at 4096 sections and processing uses fixed event storage.
