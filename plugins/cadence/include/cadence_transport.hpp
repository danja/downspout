#pragma once

#include "cadence_core_types.hpp"

namespace downspout::cadence {

double cadence_note_length_fraction(const Controls& controls);
double cadence_cycle_beats_for_controls(const Controls& controls, double beatsPerBar);
double cadence_segment_beats_for_controls(const Controls& controls, double beatsPerBar);
int cadence_segment_count_for_controls(const Controls& controls, double beatsPerBar);
double cadence_wrapped_cycle_position(double absBeats, const Controls& controls, double beatsPerBar);
int cadence_segment_index_for_time(const Controls& controls,
                                   double beatsPerBar,
                                   int segmentCount,
                                   double absBeats);
std::uint32_t cadence_frame_for_beat(double absBeatsStart,
                                     double absBeatsEnd,
                                     std::uint32_t nframes,
                                     double targetBeat);

}  // namespace downspout::cadence
