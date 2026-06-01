# Taste

Taste is a small FOSS artist recommendation system built around an explainable artist graph.

The current implementation provides:

- `taste`, a C CLI backed by SQLite.
- `tasted`, via `taste serve`, a tiny server-rendered local web app.
- Markdown vault and pack import.
- Wikidata and Wikipedia importers written for Bun.
- Graph recommendations, explanations, inspection, feedback, and edge review workflows.

## Build

```sh
make
```

## Quality gates

```sh
make test
make lint
bun --check importers/*.ts
```

## Quick start

```sh
make
./taste --db graph.sqlite pack add packs/shoegaze
./taste --db graph.sqlite recommend "Cocteau Twins" "Slowdive" "My Bloody Valentine" --mode deep-cut
./taste --db graph.sqlite explain "Cocteau Twins" "Lush"
./taste --db graph.sqlite graph inspect "Slowdive" --json
```

## Web app

```sh
./taste --db graph.sqlite serve --host 127.0.0.1 --port 8765
```

Open:

```text
http://127.0.0.1:8765
```

## Network imports

Recommendations run from local SQLite only. Network access is explicit through import commands.

```sh
./taste --db graph.sqlite import wikidata --artist "Cocteau Twins" --out vault
./taste --db graph.sqlite import wikipedia --artist "Slowdive" --out vault
./taste --db graph.sqlite vault import vault
```

You can also run importers directly:

```sh
bun importers/wikidata.ts --artist "Cocteau Twins" --out vault
bun importers/wikipedia.ts --artist "Slowdive" --out vault
```

## Vaults and packs

```sh
./taste vault validate packs/shoegaze
./taste --db graph.sqlite pack add packs/shoegaze
./taste --db graph.sqlite vault export vault-export
./taste --db graph.sqlite vault import vault-export
./taste --db graph.sqlite vault diff vault-export
```

## Review workflow

```sh
./taste --db graph.sqlite edges candidates --limit 50 --json > candidates.json
./taste --db graph.sqlite edges evidence --source "Cocteau Twins" --target "Lush" --json
./taste --db graph.sqlite edges review --file corrections.json
```

Example correction:

```json
[
  {
    "source_artist": "Cocteau Twins",
    "target_artist": "Lush",
    "relationship": "shared_scene",
    "confidence": 0.82,
    "verdict": "keep",
    "rationale": "shared 4AD dream pop and shoegaze context"
  }
]
```

Corrections apply only to existing graph edges.
