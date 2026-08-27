#!/usr/bin/env node
/**
 * campione-freesound-helper.js
 *
 * Downloads labeled drum samples from Freesound.org and tests Campione's
 * drum-recognition algorithm by loading them through the plugin's MCP HTTP
 * server.  The filenames are deliberately chosen to encode the drum category
 * so the filename-scoring path of assignDrumNotes() can succeed; the
 * acoustic-analysis path is what we are diagnosing.
 *
 * Usage:
 *   node scripts/campione-freesound-helper.js            # download + test
 *   node scripts/campione-freesound-helper.js --download # download only
 *   node scripts/campione-freesound-helper.js --test     # test only (needs manifest)
 *
 * freesound-js (third_party/freesound-js) is imported for its API surface.
 * In Node.js its built-in HTTP transport uses plain HTTP and has a known
 * response-body bug, so we drive the API directly over HTTPS below.
 */
'use strict';

// Imported for API surface reference; we bypass its Node.js transport.
require('../third_party/freesound-js/freesound');

const fs             = require('fs');
const path           = require('path');
const https          = require('https');
const http           = require('http');
const { execFileSync } = require('child_process');

// ── Configuration ──────────────────────────────────────────────────────────

const REPO_ROOT   = path.resolve(__dirname, '..');
const ENV_FILE    = path.join(REPO_ROOT, '.env');
const SAMPLE_DIR  = path.join(REPO_ROOT, 'tests', 'freesound-samples');
const MANIFEST    = path.join(SAMPLE_DIR, 'manifest.json');
const REPORT_FILE = path.join(SAMPLE_DIR, 'recognition-report.json');

const CAMPIONE_PORTS  = Array.from({ length: 10 }, (_, i) => 7220 + i);
const SAMPLES_PER_CAT = 5;

// GM percussion assignments with Freesound search queries.
// Tags are chosen to match campione_drum_map.cpp's alias table so the
// filename-scoring path has a fair chance; acoustic scoring is the focus.
const DRUM_CATEGORIES = [
  // maxDur: strict upper bound to exclude kits/loops in search results
  { tag: 'kick',          query: 'kick drum one shot',            gmNote: 36, maxDur: 1.5 },
  { tag: 'snare',         query: 'snare drum one shot',           gmNote: 38, maxDur: 1.5 },
  { tag: 'closed-hihat',  query: 'closed hi-hat one shot',        gmNote: 42, maxDur: 1.0 },
  { tag: 'open-hihat',    query: 'open hi-hat one shot',          gmNote: 46, maxDur: 3.0 },
  { tag: 'crash',         query: 'crash cymbal one shot',         gmNote: 49, maxDur: 4.0 },
  { tag: 'ride',          query: 'ride cymbal one shot',          gmNote: 51, maxDur: 4.0 },
  { tag: 'tom',           query: 'tom drum one shot',             gmNote: 41, maxDur: 1.5 },
  { tag: 'clap',          query: 'clap percussion one shot',      gmNote: 39, maxDur: 1.0 },
];

// ── .env loader ────────────────────────────────────────────────────────────

function loadEnv(envFile) {
  const env = {};
  if (!fs.existsSync(envFile)) return env;
  for (const line of fs.readFileSync(envFile, 'utf8').split('\n')) {
    const m = line.match(/^\s*([A-Z_][A-Z0-9_]*)\s*=\s*(.*?)\s*$/);
    if (m) env[m[1]] = m[2].replace(/^['"]|['"]$/g, '');
  }
  return env;
}

// ── Freesound API (HTTPS) ──────────────────────────────────────────────────
// freesound-js uses plain HTTP and has a Node.js response-body bug, so we
// implement HTTPS requests directly here.

function apiGet(apiKey, pathname, params) {
  return new Promise((resolve, reject) => {
    const qs = new URLSearchParams({ ...params, format: 'json' }).toString();
    https.get(
      { hostname: 'freesound.org', path: `/apiv2${pathname}?${qs}`,
        headers: { Authorization: `Token ${apiKey}` } },
      (res) => {
        let body = '';
        res.on('data', c => { body += c; });
        res.on('end', () => {
          if (res.statusCode >= 200 && res.statusCode < 300) {
            try { resolve(JSON.parse(body)); }
            catch (e) { reject(new Error(`JSON parse: ${e.message}`)); }
          } else {
            reject(new Error(`HTTP ${res.statusCode}: ${body.slice(0, 200)}`));
          }
        });
      }
    ).on('error', reject);
  });
}

function searchSounds(apiKey, query, maxDuration, count) {
  return apiGet(apiKey, '/search/text/', {
    query,
    filter:    `duration:[0.05 TO ${maxDuration}]`,
    fields:    'id,name,download,previews,duration',
    page_size: count * 3,  // fetch extras to allow for ID deduplication
    sort:      'rating_desc',
  });
}

// Fetch a URL to a local path following redirects.
// sendAuth controls whether the Freesound Token header is forwarded;
// we drop it on redirect to avoid leaking it to third-party hosts.
function fetchUrl(url, destPath, headers) {
  return new Promise((resolve, reject) => {
    const follow = (u, hdrs) => {
      const parsed = new URL(u);
      const lib    = parsed.protocol === 'https:' ? https : http;
      lib.get({ hostname: parsed.hostname, path: parsed.pathname + parsed.search, headers: hdrs },
        (res) => {
          if (res.statusCode === 301 || res.statusCode === 302) {
            follow(res.headers.location, {});  // drop auth on redirect
            return;
          }
          if (res.statusCode < 200 || res.statusCode >= 300) {
            reject(new Error(`HTTP ${res.statusCode}`));
            return;
          }
          const dest = fs.createWriteStream(destPath);
          res.pipe(dest);
          dest.on('finish', resolve);
          dest.on('error', reject);
        }
      ).on('error', reject);
    };
    follow(url, headers || {});
  });
}

// Download a sound from Freesound.  Attempts OAuth Bearer first (in case the
// key is an access token), then Token auth, then falls back to the HQ OGG
// preview converted to WAV via ffmpeg.
async function downloadSound(apiKey, sound, destWav) {
  const tmpOgg = destWav.replace(/\.wav$/, '.tmp.ogg');

  // 1. Try direct WAV download with Bearer auth (OAuth access token)
  if (sound.download) {
    for (const scheme of ['Bearer', 'Token']) {
      try {
        const tmp = destWav + '.tmp';
        await fetchUrl(sound.download, tmp, { Authorization: `${scheme} ${apiKey}` });
        fs.renameSync(tmp, destWav);
        return;
      } catch (_) {
        try { fs.unlinkSync(destWav + '.tmp'); } catch (_2) {}
      }
    }
  }

  // 2. Fall back: download HQ OGG preview, convert to 16-bit mono WAV via ffmpeg
  const previewUrl = sound.previews?.['preview-hq-ogg'] || sound.previews?.['preview-hq-mp3'];
  if (!previewUrl) throw new Error('no download URL and no preview available');

  await fetchUrl(previewUrl, tmpOgg, {});
  try {
    execFileSync('ffmpeg', ['-y', '-i', tmpOgg, '-ar', '44100', '-ac', '1',
                            '-sample_fmt', 's16', destWav], { stdio: 'pipe' });
  } finally {
    try { fs.unlinkSync(tmpOgg); } catch (_) {}
  }
}

// ── Campione MCP HTTP client ───────────────────────────────────────────────

let activeMcpPort = null;

function mcpCall(port, toolName, args) {
  return new Promise((resolve, reject) => {
    const payload = JSON.stringify({
      jsonrpc: '2.0', id: 1,
      method: 'tools/call',
      params: { name: toolName, arguments: args || {} },
    });
    const req = http.request(
      { hostname: 'localhost', port, method: 'POST', path: '/',
        headers: { 'Content-Type': 'application/json',
                   'Content-Length': Buffer.byteLength(payload) } },
      (res) => {
        let body = '';
        res.on('data', c => { body += c; });
        res.on('end', () => {
          try {
            const parsed = JSON.parse(body);
            if (parsed.error) return reject(new Error(parsed.error.message));
            // Unwrap MCP text content envelope
            const text = parsed.result?.content?.[0]?.text
                      ?? JSON.stringify(parsed.result);
            resolve(text);
          } catch (e) { reject(e); }
        });
      }
    );
    req.setTimeout(8000, () => { req.destroy(); reject(new Error('timeout')); });
    req.on('error', reject);
    req.write(payload);
    req.end();
  });
}

async function findCampionePort() {
  for (const port of CAMPIONE_PORTS) {
    try {
      await mcpCall(port, 'get_parameters');
      return port;
    } catch (_) {}
  }
  return null;
}

// ── Download phase ─────────────────────────────────────────────────────────

async function downloadSamples(apiKey) {
  fs.mkdirSync(SAMPLE_DIR, { recursive: true });

  // Load existing manifest to skip already-downloaded files
  let manifest = [];
  if (fs.existsSync(MANIFEST)) {
    try { manifest = JSON.parse(fs.readFileSync(MANIFEST, 'utf8')); } catch (_) {}
  }
  const cachedFiles  = new Set(manifest.map(e => e.file));
  // Track used sound IDs globally to avoid downloading the same clip under
  // multiple category labels (Freesound results often share IDs across queries).
  const usedSoundIds = new Set(manifest.map(e => e.soundId));

  for (const cat of DRUM_CATEGORIES) {
    console.log(`\nSearching: "${cat.query}" (max ${cat.maxDur}s)`);
    let results;
    try {
      results = await searchSounds(apiKey, cat.query, cat.maxDur, SAMPLES_PER_CAT);
    } catch (e) {
      console.error(`  search failed: ${e.message}`);
      continue;
    }

    const sounds = results.results || [];
    console.log(`  ${sounds.length} candidate(s) returned`);

    let catCount = 0;
    for (const snd of sounds) {
      if (catCount >= SAMPLES_PER_CAT) break;

      // Skip sound IDs already used by any category to prevent mislabeling.
      if (usedSoundIds.has(snd.id)) {
        console.log(`  [dup]    fs${snd.id} already used in another category`);
        continue;
      }

      const fname = `${cat.tag}_${catCount + 1}_fs${snd.id}.wav`;
      const dest  = path.join(SAMPLE_DIR, fname);

      if (cachedFiles.has(fname) && fs.existsSync(dest)) {
        console.log(`  [cached] ${fname}`);
        usedSoundIds.add(snd.id);
        catCount++;
        continue;
      }

      process.stdout.write(`  downloading ${fname} (${snd.duration?.toFixed(2)}s) … `);
      try {
        await downloadSound(apiKey, snd, dest);
        console.log('ok');
        manifest.push({ file: fname, tag: cat.tag, gmNote: cat.gmNote, soundId: snd.id });
        usedSoundIds.add(snd.id);
        fs.writeFileSync(MANIFEST, JSON.stringify(manifest, null, 2));
        catCount++;
      } catch (e) {
        console.log(`FAILED: ${e.message}`);
        try { fs.unlinkSync(dest); } catch (_) {}
      }
    }
    if (catCount < SAMPLES_PER_CAT)
      console.log(`  warning: only ${catCount}/${SAMPLES_PER_CAT} unique samples found`);
  }

  console.log(`\nManifest: ${MANIFEST} (${manifest.length} entries)`);
  return manifest;
}

// ── Test phase ─────────────────────────────────────────────────────────────

async function testRecognition(manifest) {
  activeMcpPort = await findCampionePort();
  if (!activeMcpPort) {
    console.error('\nNo Campione instance found on ports 7220-7229.');
    console.error('Load the plugin in your DAW, then run again with --test.');
    return;
  }
  console.log(`\nConnected to Campione on port ${activeMcpPort}`);

  const results = [];

  for (const entry of manifest) {
    const filePath = path.resolve(SAMPLE_DIR, entry.file);
    if (!fs.existsSync(filePath)) {
      console.log(`  [missing] ${entry.file}`);
      continue;
    }

    try {
      await mcpCall(activeMcpPort, 'clear_zones');
      await mcpCall(activeMcpPort, 'load_zone', { path: filePath });
      const summary  = await mcpCall(activeMcpPort, 'map_drum');
      const zonesRaw = await mcpCall(activeMcpPort, 'get_zones');

      let assigned = null;
      try {
        const zonesObj = JSON.parse(zonesRaw);
        assigned = zonesObj.zones?.[0]?.root_note ?? null;
      } catch (_) {}

      // map_drum emits "N/M zones assigned …" — any "→" means a match was made
      const anyMatch = summary.includes('\u2192');
      const correct  = assigned === entry.gmNote;
      const icon     = correct ? '\u2713' : (anyMatch ? '\u2248' : '\u2717');

      results.push({ file: entry.file, tag: entry.tag,
                     expected: entry.gmNote, assigned, correct, anyMatch, summary });
      console.log(`  ${icon} ${entry.file}: expected ${entry.gmNote}, got ${assigned ?? 'none'}`);
      if (!correct && summary) console.log(`      ${summary.trim()}`);
    } catch (e) {
      console.error(`  error [${entry.file}]: ${e.message}`);
    }
  }

  if (results.length === 0) {
    console.log('\nNo samples tested.');
    return;
  }

  // ── Per-category summary ───────────────────────────────────────────────
  const total   = results.length;
  const nOk     = results.filter(r => r.correct).length;
  const nMapped = results.filter(r => r.anyMatch).length;

  console.log('\n=== Recognition Summary ===');
  console.log(`Exact match : ${nOk}/${total} (${pct(nOk, total)}%)`);
  console.log(`Any match   : ${nMapped}/${total} (${pct(nMapped, total)}%)`);
  console.log('');

  for (const cat of DRUM_CATEGORIES) {
    const cr = results.filter(r => r.tag === cat.tag);
    if (cr.length === 0) continue;
    const ck = cr.filter(r => r.correct).length;
    const mk = cr.filter(r => r.anyMatch).length;
    console.log(`  ${cat.tag.padEnd(15)}: ${ck}/${cr.length} exact, ${mk}/${cr.length} any  (GM ${cat.gmNote})`);
  }

  fs.writeFileSync(REPORT_FILE, JSON.stringify(results, null, 2));
  console.log(`\nFull report: ${REPORT_FILE}`);
}

function pct(n, d) { return d ? Math.round(100 * n / d) : 0; }

// ── Entry point ────────────────────────────────────────────────────────────

async function main() {
  const flags    = new Set(process.argv.slice(2));
  const dlOnly   = flags.has('--download');
  const testOnly = flags.has('--test');

  const env    = loadEnv(ENV_FILE);
  const apiKey = env.FREESOUND_API_KEY;

  if (!apiKey && !testOnly) {
    console.error('FREESOUND_API_KEY not found in .env');
    process.exit(1);
  }

  let manifest;

  if (!testOnly) {
    manifest = await downloadSamples(apiKey);
  } else {
    if (!fs.existsSync(MANIFEST)) {
      console.error(`No manifest at ${MANIFEST}. Run without --test first.`);
      process.exit(1);
    }
    manifest = JSON.parse(fs.readFileSync(MANIFEST, 'utf8'));
    console.log(`Loaded ${manifest.length} entries from manifest.`);
  }

  if (!dlOnly) {
    await testRecognition(manifest);
  }
}

main().catch(e => { console.error('Fatal:', e.message); process.exit(1); });
