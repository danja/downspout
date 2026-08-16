#!/usr/bin/env bash
# Stdio↔HTTP proxy for Campione MCP server
# Finds the active instance by scanning ports 7220-7229.
find_port() {
    for p in $(seq 7220 7229); do
        r=$(curl -s --max-time 0.5 -X POST "http://localhost:$p/mcp" \
            -H 'Content-Type: application/json' \
            -d '{"jsonrpc":"2.0","id":0,"method":"ping","params":{}}' 2>/dev/null)
        [[ -n "$r" ]] && echo "$p" && return
    done
    echo "7220"
}
PORT=$(find_port)
while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    result=$(curl -s -X POST "http://localhost:$PORT/mcp" \
        -H 'Content-Type: application/json' \
        -d "$line")
    [[ -n "$result" ]] && printf '%s\n' "$result"
done
