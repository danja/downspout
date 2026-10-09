#include "downspout/cellular_automaton.hpp"
#include "downspout/test_assert.h"

#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace downspout::ca;

std::set<int> alive(const std::vector<std::uint8_t>& row)
{
    std::set<int> cells;
    for (std::size_t i = 0; i < row.size(); ++i)
        if (row[i]) cells.insert(static_cast<int>(i));
    return cells;
}

std::vector<std::uint8_t> singleCell(const int n, const int at)
{
    std::vector<std::uint8_t> row(static_cast<std::size_t>(n), 0);
    row[static_cast<std::size_t>(at)] = 1;
    return row;
}

std::vector<std::uint8_t> evolved(std::vector<std::uint8_t> row, const int rule, const int generations)
{
    evolve(row.data(), static_cast<int>(row.size()), rule, generations);
    return row;
}

void knownRows()
{
    // Rule 30 from one cell: 1, then 111, then 11001 (the textbook triangle).
    assert((alive(evolved(singleCell(15, 7), 30, 0)) == std::set<int> {7}));
    assert((alive(evolved(singleCell(15, 7), 30, 1)) == std::set<int> {6, 7, 8}));
    assert((alive(evolved(singleCell(15, 7), 30, 2)) == std::set<int> {5, 6, 9}));

    // Rule 90 from one cell is Sierpinski's triangle: 1, 101, 10001, 1010101.
    assert((alive(evolved(singleCell(15, 7), 90, 1)) == std::set<int> {6, 8}));
    assert((alive(evolved(singleCell(15, 7), 90, 2)) == std::set<int> {5, 9}));
    assert((alive(evolved(singleCell(15, 7), 90, 3)) == std::set<int> {4, 6, 8, 10}));

    // Edges wrap: a cell at 0 spreads to 4 and 1 on a ring of 5.
    assert((alive(evolved(singleCell(5, 0), 90, 1)) == std::set<int> {1, 4}));

    // Rule 0 kills everything and rule 255 fills everything; rule 204 is the identity.
    for (int generations : {1, 2, 9}) {
        assert(alive(evolved(singleCell(9, 3), 0, generations)).empty());
        assert(alive(evolved(singleCell(9, 3), 255, generations)).size() == 9);
        assert(evolved(singleCell(9, 3), 204, generations) == singleCell(9, 3));
    }

    // Generation 0 changes nothing, and generations compose.
    const auto seed = singleCell(31, 5);
    assert(evolved(seed, 110, 0) == seed);
    assert(evolved(evolved(seed, 110, 3), 110, 4) == evolved(seed, 110, 7));
}

void guards()
{
    evolve(nullptr, 8, 30, 1);  // must not crash
    std::vector<std::uint8_t> big(static_cast<std::size_t>(kMaxCells + 1), 1);
    const auto before = big;
    evolve(big.data(), static_cast<int>(big.size()), 30, 1);  // too long: left alone
    assert(big == before);
    std::vector<std::uint8_t> none;
    evolve(none.data(), 0, 30, 1);

    // The longest supported row works.
    std::vector<std::uint8_t> longest(static_cast<std::size_t>(kMaxCells), 0);
    longest[64] = 1;
    evolve(longest.data(), kMaxCells, 90, 1);
    assert((alive(longest) == std::set<int> {63, 65}));
}

void tablesAndPasses()
{
    assert(namedRule(0) == 0 && namedRule(1) == 30 && namedRule(2) == 90 && namedRule(10) == 126);
    assert(namedRule(-5) == 0 && namedRule(99) == 126);  // clamped
    assert(std::string(namedRuleName(0)) == "Off");
    assert(std::string(namedRuleName(3)) == "Rule 110");
    assert(std::string(namedRuleName(99)) == "Rule 126");
    for (int i = 1; i < kNamedRuleCount; ++i)
        assert(std::string(namedRuleName(i)) == "Rule " + std::to_string(namedRule(i)));

    assert(passForStep(0, 16) == 0 && passForStep(15, 16) == 0 && passForStep(16, 16) == 1);
    assert(passForStep(-1, 16) == -1 && passForStep(-16, 16) == -1 && passForStep(-17, 16) == -2);
    assert(passForStep(5, 0) == 5);  // a zero length is treated as 1

    assert(generationForPass(0) == 0 && generationForPass(1) == 1 && generationForPass(63) == 63);
    assert(generationForPass(64) == 0 && generationForPass(65) == 1);
    assert(generationForPass(-1) == 63);
    assert(generationForPass(5, 2) == 2 && generationForPass(4, 2) == 2 && generationForPass(3, 2) == 1);
    assert(generationForPass(1, 0) == 1);  // every < 1 is treated as 1
}

}  // namespace

int main()
{
    knownRows();
    guards();
    tablesAndPasses();
    return 0;
}
