import { outDir, requireArtist, unique, writeArtistNote } from "./common";

const args = Bun.argv.slice(2);
const artist = requireArtist(args);
const out = outDir(args);

const endpoint = "https://www.wikidata.org/w/api.php";

async function getJson<T>(url: string): Promise<T> {
  const response = await fetch(url, { headers: { "user-agent": "taste/0.1" } });
  if (!response.ok) throw new Error(`${response.status} ${response.statusText}: ${url}`);
  return response.json() as Promise<T>;
}

type SearchResponse = { search?: { id: string; label: string }[] };
type EntityResponse = {
  entities: Record<
    string,
    {
      labels?: Record<string, { value: string }>;
      aliases?: Record<string, { value: string }[]>;
      claims?: Record<string, { mainsnak?: { datavalue?: { value?: unknown } } }[]>;
      sitelinks?: Record<string, { title: string }>;
    }
  >;
};

type LabelResponse = { entities: Record<string, { labels?: Record<string, { value: string }> }> };

function entityId(value: unknown): string | undefined {
  if (!value || typeof value !== "object") return undefined;
  const candidate = value as { id?: string };
  return candidate.id;
}

async function labels(ids: string[]): Promise<Map<string, string>> {
  const result = new Map<string, string>();
  const uniqueIds = unique(ids);
  for (let i = 0; i < uniqueIds.length; i += 50) {
    const chunk = uniqueIds.slice(i, i + 50);
    const url = `${endpoint}?action=wbgetentities&ids=${encodeURIComponent(chunk.join("|"))}&props=labels&languages=en&format=json`;
    const json = await getJson<LabelResponse>(url);
    for (const [id, entity] of Object.entries(json.entities)) {
      const label = entity.labels?.en?.value;
      if (label) result.set(id, label);
    }
  }
  return result;
}

const searchUrl = `${endpoint}?action=wbsearchentities&search=${encodeURIComponent(artist)}&language=en&format=json&limit=1`;
const search = await getJson<SearchResponse>(searchUrl);
const qid = search.search?.[0]?.id;
if (!qid) throw new Error(`no Wikidata entity found for ${artist}`);

const entityUrl = `${endpoint}?action=wbgetentities&ids=${encodeURIComponent(qid)}&props=labels|aliases|claims|sitelinks&languages=en&format=json`;
const entityJson = await getJson<EntityResponse>(entityUrl);
const entity = entityJson.entities[qid];
if (!entity) throw new Error(`missing Wikidata entity ${qid}`);

const claimIds = (property: string): string[] =>
  (entity.claims?.[property] ?? [])
    .map((claim) => entityId(claim.mainsnak?.datavalue?.value))
    .filter((value): value is string => Boolean(value));

const genreIds = claimIds("P136");
const labelIds = claimIds("P264");
const associatedIds = unique([...claimIds("P463"), ...claimIds("P527"), ...claimIds("P361")]);
const mbidClaim = entity.claims?.P434?.[0]?.mainsnak?.datavalue?.value;
const labelMap = await labels([...genreIds, ...labelIds, ...associatedIds]);
const file = await writeArtistNote(out, {
  name: entity.labels?.en?.value ?? artist,
  wikidata: qid,
  musicbrainz: typeof mbidClaim === "string" ? mbidClaim : undefined,
  aliases: unique((entity.aliases?.en ?? []).map((alias) => alias.value)),
  genres: unique(genreIds.map((id) => labelMap.get(id) ?? id)),
  labels: unique(labelIds.map((id) => labelMap.get(id) ?? id)),
  associated: unique(associatedIds.map((id) => labelMap.get(id) ?? id)),
  sources: [`wikidata-${qid}`],
});

console.log(file);
