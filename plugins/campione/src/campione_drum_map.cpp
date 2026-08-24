#include "campione_drum_map.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace downspout::campione {
namespace {

// ── tokeniser ─────────────────────────────────────────────────────────────────

static bool isIndexSuffix(const std::string& tok)
{
    if (tok.empty()) return false;
    if (std::all_of(tok.begin(), tok.end(), [](char c){ return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
        return true;
    static const char* kPrefixes[] = {"v", "vel", "layer", "lyr", "rr", "round", nullptr};
    for (int i = 0; kPrefixes[i]; ++i) {
        const std::string p(kPrefixes[i]);
        if (tok.size() > p.size() && tok.compare(0, p.size(), p) == 0) {
            const auto rest = tok.substr(p.size());
            if (!rest.empty() && std::all_of(rest.begin(), rest.end(),
                [](char c){ return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
                return true;
        }
    }
    return false;
}

// Split on delimiters and camel-case boundaries; lowercase; strip index suffixes.
static std::vector<std::string> tokenize(const std::string& sourcePath)
{
    const auto lastSep = sourcePath.find_last_of("/\\");
    std::string base = (lastSep == std::string::npos) ? sourcePath : sourcePath.substr(lastSep + 1);
    const auto dot = base.rfind('.');
    if (dot != std::string::npos) base = base.substr(0, dot);

    std::vector<std::string> tokens;
    std::string cur;

    auto flush = [&]() {
        if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
    };

    for (std::size_t i = 0; i < base.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(base[i]);
        if (ch == '_' || ch == '-' || ch == ' ' || ch == '.' || ch == ',' || ch == '(' || ch == ')') {
            flush();
        } else if (std::isupper(ch) && !cur.empty()
                   && std::islower(static_cast<unsigned char>(base[i - 1]))) {
            // Split only on lowercase→uppercase: "HiHat"→"hi","hat";
            // NOT uppercase→uppercase, so "BD"→"bd", "OHH"→"ohh", "CHH"→"chh".
            flush();
            cur += static_cast<char>(std::tolower(ch));
        } else {
            cur += static_cast<char>(std::tolower(ch));
        }
    }
    flush();

    tokens.erase(std::remove_if(tokens.begin(), tokens.end(),
        [](const std::string& t){ return isIndexSuffix(t) || t.size() < 2; }),
        tokens.end());
    return tokens;
}

static bool hasToken(const std::vector<std::string>& tokens, const char* tok)
{
    for (const auto& t : tokens) if (t == tok) return true;
    return false;
}

static bool hasAny(const std::vector<std::string>& tokens,
                   std::initializer_list<const char*> candidates)
{
    for (const char* c : candidates) if (hasToken(tokens, c)) return true;
    return false;
}

// ── GM alias table ────────────────────────────────────────────────────────────
// Standard GM percussion note numbers 35–81 (MIDI channel 10).

struct TokenAlias { const char* token; int note; float score; };

// clang-format off
static const TokenAlias kAliases[] = {
    // ── Bass drum / Kick ──────────────────────────────────────────────────────
    {"kick",       35, 0.90f}, {"kick",       36, 0.80f},
    {"bd",         35, 0.85f}, {"bd",         36, 0.80f},
    {"bassdrum",   35, 0.88f}, {"bassdrum",   36, 0.75f},
    {"bass",       35, 0.30f}, {"bass",       36, 0.30f},   // weak alone

    // ── Snare ─────────────────────────────────────────────────────────────────
    {"snare",      38, 0.90f}, {"snare",      40, 0.65f},
    {"sd",         38, 0.85f}, {"sd",         40, 0.60f},
    {"sn",         38, 0.80f},

    // ── Side stick / Rim ──────────────────────────────────────────────────────
    {"sidestick",  37, 0.95f},
    {"rimshot",    37, 0.90f},
    {"rim",        37, 0.85f}, {"rim",        40, 0.55f},
    {"rims",       37, 0.85f},
    {"rs",         37, 0.80f},

    // ── Hand clap ─────────────────────────────────────────────────────────────
    {"handclap",   39, 0.95f},
    {"clap",       39, 0.92f},
    {"cp",         39, 0.80f},
    {"hc",         39, 0.70f},

    // ── Hi-hat (open/closed/pedal resolved contextually) ─────────────────────
    {"hihat",      42, 0.72f}, {"hihat",      46, 0.68f}, {"hihat",      44, 0.52f},
    {"hhat",       42, 0.70f}, {"hhat",       46, 0.65f}, {"hhat",       44, 0.50f},
    {"hh",         42, 0.68f}, {"hh",         46, 0.62f}, {"hh",         44, 0.48f},
    {"hat",        42, 0.62f}, {"hat",        46, 0.58f}, {"hat",        44, 0.46f},
    {"chh",        42, 0.85f},   // closed hi-hat abbreviation
    {"ohh",        46, 0.87f},   // open hi-hat abbreviation
    {"phh",        44, 0.84f},   // pedal hi-hat abbreviation

    // ── Low floor tom ─────────────────────────────────────────────────────────
    {"floortom",   41, 0.82f}, {"floortom",   43, 0.65f},
    {"ft",         41, 0.70f}, {"ft",         43, 0.60f},
    {"flr",        41, 0.72f},
    {"hft",        43, 0.82f},   // high floor tom

    // ── Generic toms ──────────────────────────────────────────────────────────
    {"tom",        41, 0.50f}, {"tom",        43, 0.45f}, {"tom",        45, 0.55f},
    {"tom",        47, 0.50f}, {"tom",        48, 0.50f}, {"tom",        50, 0.55f},
    {"toms",       41, 0.45f}, {"toms",       43, 0.40f}, {"toms",       45, 0.50f},
    {"toms",       47, 0.45f}, {"toms",       48, 0.45f}, {"toms",       50, 0.50f},
    // Numbered tom shortcuts common in commercial packs
    {"t1",         50, 0.72f}, {"t2",         48, 0.70f}, {"t3",         47, 0.68f},
    {"t4",         45, 0.66f}, {"t5",         43, 0.64f}, {"t6",         41, 0.62f},

    // ── Crash cymbal ──────────────────────────────────────────────────────────
    {"crash",      49, 0.85f}, {"crash",      57, 0.68f},
    {"cc",         49, 0.72f}, {"cc",         57, 0.60f},
    {"cymbal",     49, 0.48f}, {"cymbal",     57, 0.43f}, {"cymbal",     51, 0.40f},
    {"cym",        49, 0.48f}, {"cym",        57, 0.43f},
    {"cy",         49, 0.52f}, {"cy",         57, 0.47f},
    {"cr",         49, 0.60f}, {"cr",         57, 0.52f},

    // ── Ride cymbal ───────────────────────────────────────────────────────────
    {"ride",       51, 0.85f}, {"ride",       59, 0.68f},
    {"rd",         51, 0.75f}, {"rd",         59, 0.60f},
    {"rc",         51, 0.70f},

    // ── Ride bell ─────────────────────────────────────────────────────────────
    {"ridebell",   53, 0.92f},
    {"bell",       53, 0.50f},   // ambiguous alone; boosted by ride context

    // ── Chinese cymbal ────────────────────────────────────────────────────────
    {"china",      52, 0.92f},
    {"chinese",    52, 0.90f},

    // ── Splash cymbal ─────────────────────────────────────────────────────────
    {"splash",     55, 0.92f},
    {"spl",        55, 0.80f},
    {"sp",         55, 0.65f},

    // ── Tambourine ────────────────────────────────────────────────────────────
    {"tambourine", 54, 0.95f},
    {"tamb",       54, 0.90f},
    {"tambo",      54, 0.85f},
    {"tb",         54, 0.70f},

    // ── Cowbell ───────────────────────────────────────────────────────────────
    {"cowbell",    56, 0.95f},
    {"cowbel",     56, 0.92f},
    {"cow",        56, 0.78f},
    {"cb",         56, 0.78f},

    // ── Vibra slap ────────────────────────────────────────────────────────────
    {"vibraslap",  58, 0.92f},
    {"vibra",      58, 0.72f},

    // ── Bongo ─────────────────────────────────────────────────────────────────
    {"bongo",      60, 0.72f}, {"bongo",      61, 0.72f},
    {"bongos",     60, 0.68f}, {"bongos",     61, 0.68f},
    {"hbongo",     60, 0.85f},
    {"lbongo",     61, 0.85f},

    // ── Conga ─────────────────────────────────────────────────────────────────
    {"conga",      62, 0.60f}, {"conga",      63, 0.58f}, {"conga",      64, 0.55f},
    {"congas",     62, 0.55f}, {"congas",     63, 0.52f},
    {"muteconga",  62, 0.92f},
    {"openconga",  63, 0.92f},

    // ── Timbale ───────────────────────────────────────────────────────────────
    {"timbale",    65, 0.82f}, {"timbale",    66, 0.78f},
    {"timb",       65, 0.75f}, {"timb",       66, 0.70f},
    {"htimbale",   65, 0.92f},
    {"ltimbale",   66, 0.92f},

    // ── Agogo ─────────────────────────────────────────────────────────────────
    {"agogo",      67, 0.85f}, {"agogo",      68, 0.80f},

    // ── Cabasa ────────────────────────────────────────────────────────────────
    {"cabasa",     69, 0.92f},
    {"cab",        69, 0.68f},

    // ── Maracas ───────────────────────────────────────────────────────────────
    {"maracas",    70, 0.92f},
    {"maraca",     70, 0.88f},

    // ── Whistle ───────────────────────────────────────────────────────────────
    {"whistle",    71, 0.72f}, {"whistle",    72, 0.68f},
    {"swhistle",   71, 0.84f},
    {"lwhistle",   72, 0.84f},

    // ── Guiro ─────────────────────────────────────────────────────────────────
    {"guiro",      73, 0.80f}, {"guiro",      74, 0.75f},

    // ── Claves ────────────────────────────────────────────────────────────────
    {"claves",     75, 0.92f},
    {"clave",      75, 0.90f},

    // ── Wood block ────────────────────────────────────────────────────────────
    {"woodblock",  76, 0.85f}, {"woodblock",  77, 0.80f},
    {"wb",         76, 0.72f}, {"wb",         77, 0.68f},

    // ── Cuica ─────────────────────────────────────────────────────────────────
    {"cuica",      78, 0.82f}, {"cuica",      79, 0.78f},
    {"mutecuica",  78, 0.92f},
    {"opencuica",  79, 0.92f},

    // ── Triangle ─────────────────────────────────────────────────────────────
    {"triangle",   80, 0.78f}, {"triangle",   81, 0.73f},
    {"tri",        80, 0.72f}, {"tri",        81, 0.67f},
    {"mutetri",    80, 0.87f},
    {"opentri",    81, 0.87f},
};
// clang-format on

// ── GM note name table ────────────────────────────────────────────────────────

struct GmNoteName { int note; const char* name; };
static const GmNoteName kGmNoteNames[] = {
    {35,"Bass Drum 2"},   {36,"Bass Drum 1"},   {37,"Side Stick"},
    {38,"Snare 1"},       {39,"Hand Clap"},      {40,"Snare 2"},
    {41,"Low Floor Tom"}, {42,"Closed HH"},      {43,"High Floor Tom"},
    {44,"Pedal HH"},      {45,"Low Tom"},         {46,"Open HH"},
    {47,"Low-Mid Tom"},   {48,"High-Mid Tom"},    {49,"Crash 1"},
    {50,"High Tom"},      {51,"Ride 1"},           {52,"China"},
    {53,"Ride Bell"},     {54,"Tambourine"},       {55,"Splash"},
    {56,"Cowbell"},       {57,"Crash 2"},          {58,"Vibra Slap"},
    {59,"Ride 2"},        {60,"High Bongo"},        {61,"Low Bongo"},
    {62,"Mute Conga"},    {63,"Open Conga"},        {64,"Low Conga"},
    {65,"High Timbale"},  {66,"Low Timbale"},       {67,"High Agogo"},
    {68,"Low Agogo"},     {69,"Cabasa"},             {70,"Maracas"},
    {71,"Short Whistle"}, {72,"Long Whistle"},       {73,"Short Guiro"},
    {74,"Long Guiro"},    {75,"Claves"},              {76,"Hi Wood Block"},
    {77,"Lo Wood Block"}, {78,"Mute Cuica"},          {79,"Open Cuica"},
    {80,"Mute Triangle"}, {81,"Open Triangle"},
};

static const char* gmNoteName(int note)
{
    for (const auto& e : kGmNoteNames)
        if (e.note == note) return e.name;
    return "Unknown";
}

// ── scoring ────────────────────────────────────────────────────────────────────

static std::unordered_map<int, float> rawScores(const std::vector<std::string>& tokens)
{
    std::unordered_map<int, float> s;
    for (const auto& tok : tokens)
        for (const auto& alias : kAliases)
            if (tok == alias.token)
                s[alias.note] += alias.score;
    return s;
}

static void applyContextModifiers(std::unordered_map<int, float>& s,
                                   const std::vector<std::string>& tokens)
{
    // ── Hi-hat open/closed/pedal ──────────────────────────────────────────────
    const bool hasHat    = hasAny(tokens, {"hihat","hhat","hh","hat"});
    const bool hasOpen   = hasAny(tokens, {"open","op","oh"});
    const bool hasClosed = hasAny(tokens, {"closed","cls","cl","ch"});
    const bool hasPedal  = hasAny(tokens, {"pedal","pd"});
    if (hasHat) {
        if (hasOpen)   { s[46] += 0.30f; s[42] -= 0.20f; s[44] -= 0.10f; }
        if (hasClosed) { s[42] += 0.30f; s[46] -= 0.20f; s[44] -= 0.10f; }
        if (hasPedal)  { s[44] += 0.30f; s[42] -= 0.10f; s[46] -= 0.10f; }
    }

    // ── Tom pitch qualifiers ──────────────────────────────────────────────────
    const bool hasTom   = hasAny(tokens, {"tom","toms"});
    const bool hasFloor = hasAny(tokens, {"floor","floortom","ft","flr"});
    const bool hasLow   = hasAny(tokens, {"low","lo"});
    const bool hasMid   = hasAny(tokens, {"mid","med"});
    const bool hasHigh  = hasAny(tokens, {"high","hi"});
    if (hasTom) {
        if (hasFloor) {
            s[41] += 0.25f; s[43] += 0.15f;
            s[45] -= 0.15f; s[47] -= 0.15f; s[48] -= 0.15f; s[50] -= 0.15f;
            if (hasLow)  s[41] += 0.10f;
            if (hasHigh) s[43] += 0.10f;
        } else if (hasHigh) {
            s[50] += 0.25f; s[48] += 0.15f;
            s[41] -= 0.15f; s[43] -= 0.15f; s[45] -= 0.10f;
        } else if (hasLow) {
            s[45] += 0.20f; s[47] += 0.10f;
            s[50] -= 0.15f; s[48] -= 0.10f;
        }
        if (hasMid) {
            s[47] += 0.20f; s[48] += 0.15f;
            s[41] -= 0.10f; s[43] -= 0.10f;
        }
    }

    // ── Kick qualifiers ───────────────────────────────────────────────────────
    if (hasAny(tokens, {"kick","bd","bassdrum"})) {
        if (hasAny(tokens, {"acoustic","acous"})) { s[35] += 0.18f; s[36] -= 0.08f; }
        if (hasAny(tokens, {"electric","elec"}))  { s[36] += 0.18f; s[35] -= 0.08f; }
        if (hasToken(tokens, "808"))               { s[36] += 0.25f; s[35] -= 0.05f; }
    }

    // ── Snare qualifiers ──────────────────────────────────────────────────────
    if (hasAny(tokens, {"snare","sd","sn"})) {
        if (hasAny(tokens, {"electric","elec","808"})) { s[40] += 0.20f; s[38] -= 0.10f; }
        if (hasAny(tokens, {"acoustic","acous"}))       { s[38] += 0.20f; s[40] -= 0.10f; }
        if (hasAny(tokens, {"rim","rimshot"}))           { s[37] += 0.20f; s[38] -= 0.10f; }
    }

    // ── Crash numbered variants ───────────────────────────────────────────────
    if (hasAny(tokens, {"crash","cr","cc"})) {
        if (hasToken(tokens, "1")) s[49] += 0.15f;
        if (hasToken(tokens, "2")) s[57] += 0.15f;
    }

    // ── Ride bell ─────────────────────────────────────────────────────────────
    if (hasAny(tokens, {"ride","rd","rc"}) && hasToken(tokens, "bell")) {
        s[53] += 0.40f; s[51] -= 0.20f; s[59] -= 0.20f;
    }

    // ── Bongo pitch ───────────────────────────────────────────────────────────
    if (hasAny(tokens, {"bongo","bongos"})) {
        if (hasHigh || hasToken(tokens, "hi")) { s[60] += 0.20f; s[61] -= 0.10f; }
        if (hasLow)                             { s[61] += 0.20f; s[60] -= 0.10f; }
    }

    // ── Conga variants ────────────────────────────────────────────────────────
    if (hasAny(tokens, {"conga","congas"})) {
        if (hasAny(tokens, {"mute","muted"})) { s[62] += 0.20f; s[63] -= 0.10f; s[64] -= 0.10f; }
        if (hasOpen)  s[63] += 0.15f;
        if (hasLow)   { s[64] += 0.20f; s[62] -= 0.10f; s[63] -= 0.10f; }
        if (hasHigh || hasToken(tokens, "hi")) { s[62] += 0.15f; s[64] -= 0.10f; }
    }

    // ── Timbale, agogo, wood block high/low ──────────────────────────────────
    if (hasAny(tokens, {"timbale","timb"})) {
        if (hasHigh) { s[65] += 0.20f; s[66] -= 0.10f; }
        if (hasLow)  { s[66] += 0.20f; s[65] -= 0.10f; }
    }
    if (hasToken(tokens, "agogo")) {
        if (hasHigh) { s[67] += 0.20f; s[68] -= 0.10f; }
        if (hasLow)  { s[68] += 0.20f; s[67] -= 0.10f; }
    }
    if (hasAny(tokens, {"woodblock","wb"})) {
        if (hasHigh) { s[76] += 0.20f; s[77] -= 0.10f; }
        if (hasLow)  { s[77] += 0.20f; s[76] -= 0.10f; }
    }

    // ── Triangle open/mute ────────────────────────────────────────────────────
    if (hasAny(tokens, {"triangle","tri"})) {
        if (hasOpen)                           { s[81] += 0.20f; s[80] -= 0.10f; }
        if (hasAny(tokens, {"mute","muted"})) { s[80] += 0.20f; s[81] -= 0.10f; }
    }
}

// ── Hungarian algorithm ───────────────────────────────────────────────────────
// Standard O(n³) potential-based (Kuhn-Munkres) for square minimisation.
// c must be n×n. Returns assignment[row] = column.

static std::vector<int> hungarian(const std::vector<std::vector<double>>& c)
{
    const int n = static_cast<int>(c.size());
    const double kInf = 1e18;

    std::vector<double> u(n + 1, 0.0), v(n + 1, 0.0);
    std::vector<int> p(n + 1, 0), way(n + 1, 0);

    for (int i = 1; i <= n; ++i) {
        p[0] = i;
        int j0 = 0;
        std::vector<double> minVal(n + 1, kInf);
        std::vector<bool> used(n + 1, false);

        do {
            used[j0] = true;
            const int i0 = p[j0];
            int j1 = -1;
            double delta = kInf;

            for (int j = 1; j <= n; ++j) {
                if (!used[j]) {
                    const double cur = c[static_cast<std::size_t>(i0 - 1)]
                                        [static_cast<std::size_t>(j - 1)] - u[i0] - v[j];
                    if (cur < minVal[j]) { minVal[j] = cur; way[j] = j0; }
                    if (minVal[j] < delta) { delta = minVal[j]; j1 = j; }
                }
            }

            for (int j = 0; j <= n; ++j) {
                if (used[j]) { u[p[j]] += delta; v[j] -= delta; }
                else          { minVal[j] -= delta; }
            }
            j0 = j1;
        } while (p[j0] != 0);

        do {
            const int j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0 != 0);
    }

    std::vector<int> ans(static_cast<std::size_t>(n), -1);
    for (int j = 1; j <= n; ++j)
        if (p[j] != 0)
            ans[static_cast<std::size_t>(p[j] - 1)] = j - 1;
    return ans;
}

} // namespace

// ── public API ────────────────────────────────────────────────────────────────

std::vector<std::pair<int, float>> scoreFilename(const std::string& sourcePath)
{
    const auto tokens = tokenize(sourcePath);
    auto s = rawScores(tokens);
    applyContextModifiers(s, tokens);

    std::vector<std::pair<int, float>> result;
    result.reserve(s.size());
    for (const auto& kv : s)
        if (kv.second > 0.05f)
            result.emplace_back(kv.first, std::min(kv.second, 1.0f));

    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b){ return a.second > b.second; });
    return result;
}

std::vector<DrumAssignment> assignDrumNotes(const std::vector<std::string>& sourcePaths)
{
    const int n = static_cast<int>(sourcePaths.size());
    if (n == 0) return {};

    // Single-zone fast path: argmax, no collision possible.
    if (n == 1) {
        const auto cands = scoreFilename(sourcePaths[0]);
        DrumAssignment a;
        if (!cands.empty() && cands[0].second >= 0.30f) {
            a.gmNote     = cands[0].first;
            a.confidence = cands[0].second;
            a.evidence   = gmNoteName(cands[0].first);
        }
        return {a};
    }

    // Score all zones.
    std::vector<std::vector<std::pair<int,float>>> zoneScores;
    zoneScores.reserve(static_cast<std::size_t>(n));
    for (const auto& path : sourcePaths)
        zoneScores.push_back(scoreFilename(path));

    // Collect the union of candidate note values that appear in any zone.
    std::vector<int> noteList;
    for (const auto& cands : zoneScores)
        for (const auto& kv : cands)
            if (kv.second >= 0.05f &&
                std::find(noteList.begin(), noteList.end(), kv.first) == noteList.end())
                noteList.push_back(kv.first);
    std::sort(noteList.begin(), noteList.end());

    const int nNotes = static_cast<int>(noteList.size());

    // Column layout:
    //   cols 0 .. nNotes-1        → real GM notes
    //   cols nNotes .. nNotes+n-1 → dummy "unassigned" slots (one per zone)
    // The matrix must be square for the algorithm; pad with dummy rows.
    const int nCols = nNotes + n;
    const int sq    = std::max(n, nCols);

    constexpr double kRejectionThreshold = 0.30;
    constexpr double kRejectionCost      = 1.0 - kRejectionThreshold; // 0.70
    constexpr double kLargeCost          = 50.0;

    std::vector<std::vector<double>> cost(
        static_cast<std::size_t>(sq),
        std::vector<double>(static_cast<std::size_t>(sq), kLargeCost));

    for (int zi = 0; zi < n; ++zi) {
        // Build per-note score lookup for this zone.
        std::unordered_map<int, double> lookup;
        for (const auto& kv : zoneScores[static_cast<std::size_t>(zi)])
            lookup[kv.first] = static_cast<double>(kv.second);

        // Real GM note columns: cost = 1 - confidence (minimise → maximise confidence).
        for (int ni = 0; ni < nNotes; ++ni) {
            auto it = lookup.find(noteList[static_cast<std::size_t>(ni)]);
            cost[static_cast<std::size_t>(zi)][static_cast<std::size_t>(ni)] =
                (it != lookup.end()) ? (1.0 - it->second) : kLargeCost;
        }

        // Dummy "no assignment" column for this zone.
        cost[static_cast<std::size_t>(zi)][static_cast<std::size_t>(nNotes + zi)] = kRejectionCost;
    }

    // Dummy zone rows (padding): zero cost everywhere so they don't interfere.
    for (int zi = n; zi < sq; ++zi)
        for (int j = 0; j < sq; ++j)
            cost[static_cast<std::size_t>(zi)][static_cast<std::size_t>(j)] = 0.0;

    const auto assignment = hungarian(cost);

    // Build results.
    std::vector<DrumAssignment> results(static_cast<std::size_t>(n));
    for (int zi = 0; zi < n; ++zi) {
        const int col = assignment[static_cast<std::size_t>(zi)];
        if (col >= 0 && col < nNotes) {
            const int note = noteList[static_cast<std::size_t>(col)];
            float conf = 0.0f;
            for (const auto& kv : zoneScores[static_cast<std::size_t>(zi)])
                if (kv.first == note) { conf = kv.second; break; }
            results[static_cast<std::size_t>(zi)].gmNote     = note;
            results[static_cast<std::size_t>(zi)].confidence = conf;
            results[static_cast<std::size_t>(zi)].evidence   = gmNoteName(note);
        }
        // else: assigned to dummy → gmNote stays -1 (keep current note)
    }
    return results;
}

std::vector<DrumAssignment> assignDrumNotes(const std::vector<SampleZone>& zones)
{
    std::vector<std::string> paths;
    paths.reserve(zones.size());
    for (const auto& z : zones) paths.push_back(z.sourcePath);
    return assignDrumNotes(paths);
}

} // namespace downspout::campione
