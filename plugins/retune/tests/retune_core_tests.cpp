#include "retune_engine.hpp"
#include "retune_scale.hpp"

#include "downspout/test_assert.h"

#include <cmath>
#include <string>
#include <vector>

using namespace downspout::retune;

namespace {

bool near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) < tol; }

void testParseCentsAndRatios()
{
    const std::string text =
        "! just.scl\n"
        "5-limit major\n"
        " 7\n"
        "!\n"
        " 9/8\n"
        " 5/4   third\n"
        " 4/3\n"
        " 3/2\n"
        " 5/3\n"
        " 15/8\n"
        " 2\n";
    const auto scale = parseScl(text);
    assert(scale.has_value());
    assert(scale->description == "5-limit major");
    assert(scale->size() == 7);
    assert(near(scale->cents[0], 203.910002, 1e-4));
    assert(near(scale->cents[3], 701.955001, 1e-4));
    assert(near(scale->period(), 1200.0));

    const auto cents = parseScl("x\n2\n 100.0\n 1200.000\n");
    assert(cents.has_value() && near(cents->cents[0], 100.0));

    assert(parseScl("blank description\n1\n 2/1\n").has_value());
    assert(parseScl("\n1\n 2/1\n").has_value());  // description may be empty
}

void testParseRejectsMalformed()
{
    assert(!parseScl(""));
    assert(!parseScl("desc\n"));
    assert(!parseScl("desc\n3\n 100.0\n 200.0\n"));          // too few pitches
    assert(!parseScl("desc\n1\n 100.0\n 200.0\n"));          // junk after the pitches
    assert(!parseScl("desc\n0\n"));                          // empty scale
    assert(!parseScl("desc\n1\n abc\n"));                    // not a number
    assert(!parseScl("desc\n1\n 3/0\n"));                    // zero denominator
    assert(!parseScl("desc\n1\n -3/2\n"));                   // negative ratio
    assert(!parseScl("desc\n1\n -100.0\n"));                 // non-positive period
    assert(!parseScl("desc\n1.5\n 100.0\n"));                // fractional count
    assert(!parseScl("desc\n99999\n 100.0\n"));              // absurd count
}

void testEqualTemperamentNeedsNoBend()
{
    const Scale et = equalTemperament();
    for (int note = 0; note < 128; ++note) {
        const auto t = tuneNote(et, 60, note, 2.0);
        assert(t.has_value());
        assert(t->carrier == note);
        assert(t->bend == 8192);
    }
}

void testJustIntonationBend()
{
    // Ratio 5/4 is 386.31 cents: carrier 4 semitones up, -13.69 cents of bend.
    Scale just;
    just.cents = {111.73, 203.91, 315.64, 386.31, 498.04, 590.22, 701.96, 813.69, 884.36, 996.09, 1088.27, 1200.0};
    const auto t = tuneNote(just, 60, 64, 2.0);
    assert(t.has_value());
    assert(t->carrier == 64);
    const double bendCents = (t->bend - 8192) / 8192.0 * 200.0;
    assert(near(bendCents, -13.69, 0.05));
    // The root and the octave stay put.
    assert(tuneNote(just, 60, 60, 2.0)->bend == 8192);
    assert(tuneNote(just, 60, 72, 2.0)->bend == 8192);
}

void testNonOctaveScalePeriodAndNegativeOffsets()
{
    // Bohlen-Pierce style: 13 degrees to a 3/1 period (1901.955 cents).
    Scale bp;
    for (int i = 1; i <= 13; ++i) bp.cents.push_back(1901.955 * i / 13.0);
    assert(near(noteCents(bp, 60, 60 + 13), 1901.955, 1e-6));
    assert(near(noteCents(bp, 60, 60 - 13), -1901.955, 1e-6));
    assert(near(noteCents(bp, 60, 60 - 1), 1901.955 * 12 / 13.0 - 1901.955, 1e-6));
}

void testBendRangeAndCarrierLimits()
{
    Scale wide;
    wide.cents = {1200.0};  // one degree per octave: neighbouring keys are an octave apart
    // Notes far from the root run off the end of the MIDI range.
    assert(!tuneNote(wide, 60, 127, 2.0).has_value());
    // A correction larger than the bend range is refused rather than clipped.
    Scale quarter;
    quarter.cents = {50.0, 100.0};
    assert(!tuneNote(quarter, 60, 61, 0.1).has_value());
    assert(tuneNote(quarter, 60, 61, 2.0).has_value());
    assert(!tuneNote(quarter, 60, 61, 0.0).has_value());
}

void testEngineAllocatesChannelPerNote()
{
    Scale just;
    just.cents = {111.73, 203.91, 315.64, 386.31, 498.04, 590.22, 701.96, 813.69, 884.36, 996.09, 1088.27, 1200.0};
    Engine e;
    e.setScale(just);
    std::vector<MidiOut> out;
    e.noteOn(0, 60, 100, out);
    e.noteOn(0, 64, 90, out);
    assert(out.size() == 4);
    assert(out[0].status >> 4 == 0xE && out[1].status >> 4 == 0x9);
    assert((out[0].status & 15) == (out[1].status & 15));
    assert((out[0].status & 15) != (out[2].status & 15));  // second note, different channel
    assert(out[1].data1 == 60 && out[3].data1 == 64);
    assert(e.activeNotes() == 2);

    out.clear();
    e.noteOff(0, 64, out);
    assert(out.size() == 1 && out[0].status >> 4 == 0x8 && out[0].data1 == 64);
    assert(e.activeNotes() == 1);

    out.clear();
    e.noteOff(0, 64, out);  // unmatched note-off is ignored
    assert(out.empty());

    out.clear();
    e.noteOn(0, 62, 0, out);  // velocity 0 is a note-off for an unheld note: nothing
    assert(out.empty());
}

void testEngineStealsOldestWhenPoolFull()
{
    Engine e;
    Settings s;
    s.firstChannel = 1;
    s.lastChannel = 3;  // three-note pool
    e.setSettings(s);
    std::vector<MidiOut> out;
    for (int note = 60; note < 63; ++note) e.noteOn(0, note, 100, out);
    assert(e.activeNotes() == 3);

    out.clear();
    e.noteOn(0, 70, 100, out);
    // note-off for the oldest (60), then bend and note-on.
    assert(out.size() == 3);
    assert(out[0].status >> 4 == 0x8 && out[0].data1 == 60);
    assert(e.activeNotes() == 3);
    out.clear();
    e.noteOff(0, 60, out);  // the stolen note is gone; its off does nothing
    assert(out.empty());
}

void testRetriggerAndAllNotesOff()
{
    Engine e;
    std::vector<MidiOut> out;
    e.noteOn(0, 60, 100, out);
    out.clear();
    e.noteOn(0, 60, 100, out);  // same note again: off, bend, on
    assert(out.size() == 3 && out[0].status >> 4 == 0x8);
    assert(e.activeNotes() == 1);

    e.noteOn(0, 62, 100, out);
    out.clear();
    e.allNotesOff(out);
    assert(out.size() == 2);
    assert(e.activeNotes() == 0);
}

void testUntunableNoteIsDroppedWithItsOff()
{
    Scale wide;
    wide.cents = {1200.0};
    Engine e;
    e.setScale(wide);
    std::vector<MidiOut> out;
    e.noteOn(0, 127, 100, out);
    assert(out.empty() && e.activeNotes() == 0);
    e.noteOff(0, 127, out);
    assert(out.empty());
}

void testSameNoteOnDifferentInputChannelsAreSeparate()
{
    Engine e;
    std::vector<MidiOut> out;
    e.noteOn(0, 60, 100, out);
    e.noteOn(1, 60, 100, out);
    assert(e.activeNotes() == 2);
    out.clear();
    e.noteOff(0, 60, out);
    assert(out.size() == 1 && e.activeNotes() == 1);
}

void testOtherMessageRouting()
{
    Engine e;
    Settings s;
    s.firstChannel = 1;
    s.lastChannel = 3;
    e.setSettings(s);
    std::vector<MidiOut> out;
    e.noteOn(2, 60, 100, out);
    const int noteChannel = out.back().status & 15;

    out.clear();
    e.other(0xB2, 64, 127, out);  // sustain pedal: to all three pool channels
    assert(out.size() == 3);
    for (const MidiOut& m : out) assert((m.status >> 4) == 0xB && m.data1 == 64 && m.data2 == 127);
    assert((out[0].status & 15) == 1 && (out[2].status & 15) == 3);

    out.clear();
    e.other(0xC2, 5, 0, out);  // program change broadcast
    assert(out.size() == 3 && (out[0].status >> 4) == 0xC);

    out.clear();
    e.other(0xA2, 60, 40, out);  // poly aftertouch follows the note
    assert(out.size() == 1 && (out[0].status & 15) == noteChannel && out[0].data1 == 60);
    out.clear();
    e.other(0xA2, 61, 40, out);  // for a note that is not held: nothing
    assert(out.empty());

    out.clear();
    e.other(0xE2, 0, 64, out);  // incoming pitch bend would undo the tuning
    e.other(0xD2, 50, 0, out);  // channel aftertouch has no single target
    assert(out.empty());

    e.other(0xB2, 123, 0, out);  // all notes off releases held notes
    assert(out.size() == 1 && (out[0].status >> 4) == 0x8 && e.activeNotes() == 0);
}

}  // namespace

int main()
{
    testParseCentsAndRatios();
    testParseRejectsMalformed();
    testEqualTemperamentNeedsNoBend();
    testJustIntonationBend();
    testNonOctaveScalePeriodAndNegativeOffsets();
    testBendRangeAndCarrierLimits();
    testEngineAllocatesChannelPerNote();
    testEngineStealsOldestWhenPoolFull();
    testRetriggerAndAllNotesOff();
    testUntunableNoteIsDroppedWithItsOff();
    testSameNoteOnDifferentInputChannelsAreSeparate();
    testOtherMessageRouting();
    return 0;
}
