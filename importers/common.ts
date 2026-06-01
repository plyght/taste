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

export async function writeArtistNote(out: string, note: ArtistNote): Promise<string> {
  const dir = `${out.replace(/\/$/, "")}/artists`;
  await Bun.$`mkdir -p ${dir}`;
  const file = `${dir}/${slugify(note.name)}.md`;
  const list = (items: string[]) => items.map((item) => `  - ${item}`).join("\n");
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

export function requireArtist(args: string[]): string {
  const artist = argValue(args, "--artist") ?? args[0];
  if (!artist) {
    throw new Error("missing artist; pass --artist \"Artist Name\"");
  }
  return artist;
}

export function outDir(args: string[]): string {
  return argValue(args, "--out") ?? "vault";
}

export function cacheDir(args: string[]): string {
  return argValue(args, "--cache") ?? ".cache/taste";
}

export async function cachedJson<T>(cache: string, key: string, url: string): Promise<T> {
  await Bun.$`mkdir -p ${cache}`;
  const file = `${cache.replace(/\/$/, "")}/${slugify(key)}.json`;
  const cached = Bun.file(file);
  if (await cached.exists()) {
    return cached.json() as Promise<T>;
  }
  const response = await fetch(url, { headers: { "user-agent": "taste/0.1" } });
  if (!response.ok) throw new Error(`${response.status} ${response.statusText}: ${url}`);
  const json = (await response.json()) as T;
  await Bun.write(file, JSON.stringify(json, null, 2));
  return json;
}
