#!/usr/bin/env bash
# Stdio↔HTTP proxy for Campione MCP server
while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    result=$(curl -s -X POST http://localhost:7220/mcp \
        -H 'Content-Type: application/json' \
        -d "$line")
    [[ -n "$result" ]] && printf '%s\n' "$result"
done
