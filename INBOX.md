- bore, the output level is very low, also the register capture still hasn't enough variation, it should sometime squawk onto the wrong register like a real clarinet

- drumgen, the templates are thin on notes, they need to be more complete

* create a stereo enhancer

- extend chorale to also be a flanger and phaser

* bassops doesn't seem to work very well as a sidechain ducker. Can you check any stereo input will be carried to the output, however processed

* loopdelay pingpong doesn't appear to do anything

* randomise buttons on all plugins that don't have one already (if it would makee sense)

## PadSeq - new plugin, sequencer based around Lanchpad

- grid-based sequence editor like standard midi editors except that it will support alternate scales, including map to General Midi drums
- note number on x axis, time on y axis
-
- based around a paged 8x8 grid, with sequence length 1 to 32 steps, default 16
- Use Xololo and Luma plugins for reference, scales as common scales list
- Plugin can be used without Launchpad, but the plugin UI will have controls corresponding to those of the Launchpad.

### Controls : top

left to right

0. pattern view down a step
1. pattern view up a step
2. previous page
3. next page
4. previous scale
5. next scale
6. randomise (modify current pattern if there is one, otherwise generate new)
7. clear pattern & reset (panic)

Controls along the right column will correspond to notes in the grid. When running, pressing a key will send the corresponding note on message with note off timed to the grid step.

- add launchpad support to xololo
