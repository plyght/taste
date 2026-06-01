export type ArtistNote = {
  name: string;
  wikidata?: string;
  musicbrainz?: string;
  aliases: string[];
  genres: string[];
  labels: string[];
  associated: string[];
  sources: string[];
};

export function slugify(value: string): string {
  return value
    .toLowerCase()
    .normalize("NFKD")
    .replace(/[\u0300-\u036f]/g, "")
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-|-$/g, "");
}

export function unique(values: string[]): string[] {
  return [...new Set(values.map((value) => value.trim()).filter(Boolean))];
}

export function argValue(args: string[], name: string): string | undefined {
  const index = args.indexOf(name);
  if (index === -1) return undefined;
  return args[index + 1];
}

export async function writeArtistNote(
  out: string,
  note: ArtistNote,
): Promise<string> {
  const dir = `${out.replace(/\/$/, "")}/artists`;
  await Bun.$`mkdir -p ${dir}`;
  const file = `${dir}/${slugify(note.name)}.md`;
  const existing = await readArtistNote(file);
  if (existing) {
    note = {
      name: note.name || existing.name,
      wikidata: note.wikidata || existing.wikidata,
      musicbrainz: note.musicbrainz || existing.musicbrainz,
      aliases: unique([...existing.aliases, ...note.aliases]),
      genres: unique([...existing.genres, ...note.genres]),
      labels: unique([...existing.labels, ...note.labels]),
      associated: unique([...existing.associated, ...note.associated]),
      sources: unique([...existing.sources, ...note.sources]),
    };
  }
  const list = (items: string[]) =>
    items.map((item) => `  - ${item}`).join("\n");
  const body = `---
type: artist
name: ${note.name}
wikidata: ${note.wikidata ?? ""}
musicbrainz: ${note.musicbrainz ?? ""}
aliases:
${list(note.aliases)}
genres:
${list(note.genres)}
labels:
${list(note.labels)}
associated:
${list(note.associated)}
sources:
${list(note.sources)}
---

# ${note.name}

## Links
${[...note.genres, ...note.labels, ...note.associated].map((value) => `- [[${value}]]`).join("\n")}
`;
  await Bun.write(file, body);
  return file;
}

async function readArtistNote(file: string): Promise<ArtistNote | undefined> {
  const source = Bun.file(file);
  if (!(await source.exists())) return undefined;
  const text = await source.text();
  const frontmatter = text.match(/^---\n([\s\S]*?)\n---/);
  if (!frontmatter) return undefined;
  const note: ArtistNote = {
    name: "",
    aliases: [],
    genres: [],
    labels: [],
    associated: [],
    sources: [],
  };
  let section = "";
  for (const rawLine of frontmatter[1].split(/\r?\n/)) {
    const line = rawLine.trim();
    if (!line) continue;
    if (line.startsWith("- ")) {
      const value = line.slice(2).trim();
      if (section === "aliases") note.aliases.push(value);
      else if (section === "genres") note.genres.push(value);
      else if (section === "labels") note.labels.push(value);
      else if (section === "associated") note.associated.push(value);
      else if (section === "sources") note.sources.push(value);
      continue;
    }
    const match = line.match(/^([A-Za-z_]+):\s*(.*)$/);
    if (!match) continue;
    const [, key, value] = match;
    section = value ? "" : key;
    if (key === "name") note.name = value;
    else if (key === "wikidata") note.wikidata = value || undefined;
    else if (key === "musicbrainz") note.musicbrainz = value || undefined;
  }
  return note.name ? note : undefined;
}

export function requireArtist(args: string[]): string {
  const artist = argValue(args, "--artist") ?? args[0];
  if (!artist) {
    throw new Error('missing artist; pass --artist "Artist Name"');
  }
  return artist;
}

export function outDir(args: string[]): string {
  return argValue(args, "--out") ?? "vault";
}

export function cacheDir(args: string[]): string {
  return argValue(args, "--cache") ?? ".cache/taste";
}

export async function cachedJson<T>(
  cache: string,
  key: string,
  url: string,
): Promise<T> {
  await Bun.$`mkdir -p ${cache}`;
  const file = `${cache.replace(/\/$/, "")}/${slugify(key)}.json`;
  const cached = Bun.file(file);
  if (await cached.exists()) {
    return cached.json() as Promise<T>;
  }
  await sleep(1200);
  const response = await fetchWithRetry(url);
  if (!response.ok)
    throw new Error(`${response.status} ${response.statusText}: ${url}`);
  const json = (await response.json()) as T;
  await Bun.write(file, JSON.stringify(json, null, 2));
  return json;
}

async function fetchWithRetry(url: string): Promise<Response> {
  let wait = 2000;
  for (let attempt = 0; attempt < 5; attempt++) {
    if (attempt > 0) await sleep(wait);
    const response = await fetch(url, {
      headers: {
        "user-agent": "taste/0.1 (local-first artist graph importer)",
      },
    });
    if (response.status !== 429 && response.status < 500) return response;
    wait *= 2;
  }
  return fetch(url, {
    headers: {
      "user-agent": "taste/0.1 (local-first artist graph importer)",
    },
  });
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}
