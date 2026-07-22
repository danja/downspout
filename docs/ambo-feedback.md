Thanks to [u/ChapelHeel66 on Reddit](https://www.reddit.com/user/ChapelHeel66/)
for testing Ambo and providing the detailed feedback below.

Just to get things rolling, I tried Ambo on a synth sequence in StudioOne 7 on Windows.  Then tried it on some pads.  My Notes:

1.  Seemed to be fully functional.  No crashes.  All the parameters seemed to be working.  
2,  At extreme ends, sometimes you cannot grab the little slider balls.  You can click in the bars and get them back, but all the way right and all the way left I could not grab them without clicking in the bars to get them back. This is also true in the four corners of the XY panel  
3. Audible clicking if changing the chain while transport is running.  It's just one click and maybe it is unavoidable since the modules are shifting.  
4. There appears to be some text under the wet slider bar below the XY panel, but it is obscured.  A little under the "wet" word and three little places at the far right of the bar.  
5. I'm a little confused by what the wet and return levels are doing.  Even when XY is all the way bottom left (0% wet and 0% feedback, there are wet and return levels.  I thought maybe this was waiting for something in a buffer to bleed off, but that does not appear to be true.  
6.  Wet and return levels always show exactly the same, no matter the chain or parameters.  All in all, I can't tie out what the wet and return levels are doing to anything that's happening in the plugin.  
7.  Suggest adding double click or control click to return sliders to defaults.  
8. Some crackling/static when moving Time, Shimmer, Drive and Mix sliders rapidly in Drift mode.  However, which ones crackle seems to be chain-dependent.  So, I don't know if it is related to the effect, or the specific parameter slider itself.  For instance, in Fracture, Drive is first and does not crackle, but it crackles when it is column2 row3 of the sliders in Drift mode.  In Fracture, Tape is in column2 row3 and it crackles when rapidly moving the slider, but it does not when in Bloom (where it is first).  
9.  Bypass did not work in StudioOne's plugin window.  
10. I don't really understand why the wet and return levels are in the upper right.  They are showing the same things as bottom right, but on a smaller scale.  It's a little strange to have a duplicate set of levels but on a different scale.  
11.  It also was not intuitive to me that I could not actually slide either of the wets.  Because they are similarly shaped to the actual sliders, I did not know they were levels.  I thought I could grab either of the wet controls, but in reality I had to go to the mix control.  
12.  Personally I find it a little disorienting to have the chains go full left to right (one through six) and then have the actual controls for those chains go L R, new line, L R, new line.  I kept reaching for column1 row2 to control the second parameter, for example.  My instinct was also to try to adjust the levels at the actual chain level boxes.  I couldn't figure out why I couldn't slide them up or down.  It turns out they are just level meters, but I'm wondering why I need to know their levels, when immediately below I set the parameters to set the levels.  I think it might be better to have those chain boxes actually be the slider controls, up or down and eliminate the L to R controls for those same parameters.  Then only have Feedback, Mix and Output sliders , since those are not chain dependent.  
12.  Last, what exactly are the circles in the XY panel doing?  Since they are green and brown, they should relate to the wet and return levels.  But as I mentioned, I'm seeing wet and return exactly the same on both level meters regardless of what the plugin is doing, but the green circle is much bigger than the brown one as I'm looking at it now.  And more importantly, what do the circles tell us that the two sets of wet and return levels do not?  I feel like that's three different wet and return levels, all of which indicate something different.  
  
For Ambo, I think most of the functionality is there, except for some minor things, and it looks nice overall.  I like the idea.  But I think you have some design choices to tighten up.  
  
  I will check out a few others, but I assume that you have more or less used the same setup for them, so I'm guessing my comments above will have some global application.  Hope some of this helps.  

## Proposed actionable changes

1. Make every slider and the XY handle reachable at its minimum and maximum:
   inset the drawn handles from the visual bounds and enlarge their hit areas.
2. Make the six chain blocks the six module controls. Drag a block vertically to
   change its value, and keep their left-to-right order identical to the selected
   chain. Remove the duplicated two-column module-slider grid.
3. Keep only Feedback, Mix, and Output as conventional sliders below the chain.
   Label the XY pad explicitly as a combined Mix/Feedback performance control.
4. Remove the decorative wet/return circles and both duplicate meter pairs. They
   look interactive and do not add a trustworthy or distinct signal reading.
5. Correct the processor status semantics: wet activity measures the audible wet
   contribution after Mix, while return activity measures the signal actually
   injected by Feedback. The two output parameters remain available to hosts but
   are no longer used as ambiguous UI controls.
6. Add Control-click reset-to-default for module blocks, utility sliders, and the
   XY pad. (DPF's mouse event has modifiers but no portable double-click count.)
7. Smooth continuous parameters in the portable processor to prevent zipper
   noise during fast automation or dragging.
8. On a chain change, briefly crossfade through the dry signal before changing
   topology, avoiding the discontinuity that caused a click during playback.
9. Add a DPF-designated `dpf_bypass` parameter and implement a smoothed dry bypass
   in the portable processor so Studio One's plugin-window bypass is functional.
10. Add deterministic core coverage for activity-meter semantics, rapid parameter
    changes, chain transitions, and bypass behavior; update Ambo's local docs to
    describe the revised controls and transition assumptions.

### Verification note

The Ambo core tests and `ambo-vst3` target pass. Steinberg's validator recognizes
the new Bypass parameter as a toggle and passes 46 of 47 tests. Its remaining
"bypass parameter is not in sync in the controller" failure is also present in
the repository's existing Basilico bundle, which uses the same DPF bypass
designation. Resolving that DPF-wide controller-state limitation would require a
shared framework change and is therefore outside this plugin-local change.
