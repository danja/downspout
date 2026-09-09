#!/usr/bin/env python3
"""
Add "${PROJECT_SOURCE_DIR}/include" to the UI-facing target_include_directories
in each plugin CMakeLists.txt that does not already have it.

Targets that get the new path: <plugin> and <plugin>-ui (but NOT <plugin>-dsp,
<plugin>_core, or any other target).

Strategy:
  - Find each target_include_directories(<ui-target> PUBLIC ...) block.
  - Insert the new path as the last entry before the closing paren,
    preserving the indentation style of the existing entries.
"""

import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PLUGINS_DIR = REPO_ROOT / "plugins"

# Plugins that need updating (confirmed by prior check)
NEEDS_UPDATE = {
    "ambo", "arpgen", "basilico", "bassops", "bubbles", "cadence", "canticle",
    "chipper", "counterpointer", "damiano", "drumkit", "e-mix", "floozy",
    "flues-synth-driver", "gater", "gremlin", "gremlin-driver", "ground",
    "lifeform", "luma", "melgen", "midiscribe", "m-mix", "orchid", "paunchlad",
    "p-mix", "rift", "sidecar", "skream", "syrinx", "t-mix", "tuney-vst",
    "worms", "xoxolo",
}

NEW_ENTRY = '"${PROJECT_SOURCE_DIR}/include"'


def insert_into_block(text: str, target_name: str) -> str:
    """
    Find target_include_directories(<target_name> PUBLIC ...) and insert
    NEW_ENTRY before the closing ')'. Returns the modified text.
    Handles both indented multi-line blocks and compact single-line forms.
    """
    # We scan for the opening of the call, then locate the balanced closing paren.
    search = f"target_include_directories({target_name} PUBLIC"
    # Also handle case where target name is on same "word" boundary
    pos = 0
    result_parts = []

    while True:
        idx = text.find(search, pos)
        if idx == -1:
            result_parts.append(text[pos:])
            break

        # Found a match. Extract the whole call by finding the balanced paren.
        # The opening ( is already included in 'search' prefix (the one before PUBLIC)
        # Actually search doesn't include opening paren — "target_include_directories(<target> PUBLIC"
        # so we need to go back to find the (
        open_paren = text.rfind('(', pos, idx + len(search))
        if open_paren == -1:
            result_parts.append(text[pos:idx+1])
            pos = idx + 1
            continue

        # Find the matching closing paren
        depth = 0
        j = open_paren
        close_paren = -1
        while j < len(text):
            if text[j] == '(':
                depth += 1
            elif text[j] == ')':
                depth -= 1
                if depth == 0:
                    close_paren = j
                    break
            j += 1

        if close_paren == -1:
            result_parts.append(text[pos:idx+1])
            pos = idx + 1
            continue

        call = text[open_paren:close_paren + 1]
        call_start = open_paren

        # Skip if already has PROJECT_SOURCE_DIR/include
        if '${PROJECT_SOURCE_DIR}/include' in call:
            result_parts.append(text[pos:close_paren + 1])
            pos = close_paren + 1
            continue

        # Determine indentation for the new entry by looking at existing entries
        # Find lines within the call that have quoted paths
        entry_indent = None
        close_indent = None
        for line in call.split('\n')[1:]:
            stripped = line.strip()
            if stripped.startswith('"') and ('CMAKE_CURRENT_SOURCE_DIR' in stripped or
                                              'CMAKE_SOURCE_DIR' in stripped or
                                              'PROJECT_SOURCE_DIR' in stripped):
                entry_indent = line[:len(line) - len(line.lstrip())]
                break
        if entry_indent is None:
            # fallback: 8 spaces
            entry_indent = '        '

        # Find what indentation is used for the closing paren
        # Look at the character(s) before close_paren going back to newline
        before_close = text[:close_paren]
        newline_pos = before_close.rfind('\n')
        if newline_pos != -1:
            close_indent = text[newline_pos + 1:close_paren]
        else:
            close_indent = ''

        # Check if closing paren is on its own line (only whitespace between \n and ))
        if close_indent.strip() == '':
            # Multi-line: insert new entry as a new line before the closing line
            # text[pos:close_paren] is everything up to (not including) the )
            # We need to insert: \n<entry_indent><NEW_ENTRY>
            # right before the \n that starts the close-paren line
            insert_pos = close_paren  # insert before the )
            new_text_segment = (
                text[pos:newline_pos + 1]           # everything up to and including \n before )
                + entry_indent + NEW_ENTRY + '\n'    # new entry line
                + close_indent + ')'                 # closing ) with its original indent
            )
            result_parts.append(new_text_segment)
            pos = close_paren + 1
        else:
            # Single-line or close paren right after last entry
            # Just insert before )
            new_text_segment = (
                text[pos:close_paren]
                + '\n' + entry_indent + NEW_ENTRY
                + '\n' + close_indent + ')'
            )
            result_parts.append(new_text_segment)
            pos = close_paren + 1

    return ''.join(result_parts)


def process_cmake(path: Path, plugin_name: str) -> bool:
    """Return True if the file was modified."""
    text = path.read_text()

    if '${PROJECT_SOURCE_DIR}/include' in text:
        print(f"  SKIP (already has it): {plugin_name}")
        return False

    # CMake target name variants (hyphens vs underscores)
    cmake_name = plugin_name.replace("-", "_")
    names_to_try = list(dict.fromkeys([plugin_name, cmake_name]))  # deduplicate, preserve order

    new_text = text
    for tname in names_to_try:
        for suffix in ["", "-ui"]:
            full_target = tname + suffix
            new_text = insert_into_block(new_text, full_target)

    changed = new_text != text
    if changed:
        path.write_text(new_text)
        print(f"  UPDATED: {plugin_name}")
    else:
        print(f"  NO MATCH: {plugin_name} (check manually)")

    return changed


def main():
    updated = []
    no_match = []

    for plugin_dir in sorted(PLUGINS_DIR.iterdir()):
        if not plugin_dir.is_dir():
            continue
        plugin_name = plugin_dir.name
        if plugin_name not in NEEDS_UPDATE:
            continue

        cmake = plugin_dir / "CMakeLists.txt"
        if not cmake.exists():
            print(f"  MISSING CMakeLists.txt: {plugin_name}")
            continue

        result = process_cmake(cmake, plugin_name)
        if result:
            updated.append(plugin_name)
        else:
            no_match.append(plugin_name)

    print(f"\nUpdated {len(updated)} files: {updated}")
    print(f"No match / skipped {len(no_match)}: {no_match}")


if __name__ == "__main__":
    main()
