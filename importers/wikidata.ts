import {
  cachedJson,
  cacheDir,
  outDir,
  requireArtist,
  unique,
  writeArtistNote,
} from "./common";

const args = Bun.argv.slice(2);
const artist = requireArtist(args);
const out = outDir(args);
const cache = cacheDir(args);
const includeRelated = args.includes("--related");

const endpoint = "https://www.wikidata.org/w/api.php";

type SearchResponse = {
  search?: { id: string; label: string; description?: string }[];
};
type EntityResponse = {
  entities: Record<
    string,
    {
      labels?: Record<string, { value: string }>;
      aliases?: Record<string, { value: string }[]>;
      claims?: Record<
        string,
        { mainsnak?: { datavalue?: { value?: unknown } } }[]
      >;
      sitelinks?: Record<string, { title: string }>;
    }
  >;
};

type LabelResponse = {
  entities: Record<string, { labels?: Record<string, { value: string }> }>;
};
type SparqlResponse = {
  results?: {
    bindings?: {
      artist?: { value: string };
      artistLabel?: { value: string };
      mbid?: { value: string };
      fact?: { value: string };
      factLabel?: { value: string };
    }[];
  };
};

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
    let json: LabelResponse;
    try {
      json = await cachedJson<LabelResponse>(
        cache,
        `wikidata-labels-${chunk.join("-")}`,
        url,
      );
    } catch {
      continue;
    }
    for (const [id, entity] of Object.entries(json.entities)) {
      const label = entity.labels?.en?.value;
      if (label) result.set(id, label);
    }
  }
  return result;
}

async function chooseMusicEntity(): Promise<string | undefined> {
  const searchUrl = `${endpoint}?action=wbsearchentities&search=${encodeURIComponent(artist)}&language=en&format=json&limit=8`;
  const search = await cachedJson<SearchResponse>(
    cache,
    `wikidata-search-${artist}`,
    searchUrl,
  );
  const ids = unique((search.search ?? []).map((result) => result.id));
  if (!ids.length) return undefined;
  const entityUrl = `${endpoint}?action=wbgetentities&ids=${encodeURIComponent(ids.join("|"))}&props=claims|sitelinks|labels|aliases&languages=en&format=json`;
  const candidates = await cachedJson<EntityResponse>(
    cache,
    `wikidata-search-entities-${artist}`,
    entityUrl,
  );
  let fallback = ids[0];
  let best = "";
  let bestScore = -1;
  for (const id of ids) {
    const entity = candidates.entities[id];
    if (!entity) continue;
    const claims = entity.claims ?? {};
    let score = 0;
    if (claims.P434?.length) score += 5;
    if (claims.P136?.length) score += 4;
    if (claims.P264?.length) score += 3;
    if (claims.P175?.length || claims.P361?.length || claims.P527?.length)
      score += 1;
    if (entity.sitelinks?.enwiki) score += 1;
    const label = entity.labels?.en?.value ?? "";
    if (label.toLowerCase() === artist.toLowerCase()) score += 2;
    if (score > bestScore) {
      best = id;
      bestScore = score;
    }
  }
  return best || fallback;
}

const qid = await chooseMusicEntity();
if (!qid) throw new Error(`no Wikidata entity found for ${artist}`);

const entityUrl = `${endpoint}?action=wbgetentities&ids=${encodeURIComponent(qid)}&props=labels|aliases|claims|sitelinks&languages=en&format=json`;
const entityJson = await cachedJson<EntityResponse>(
  cache,
  `wikidata-entity-${qid}`,
  entityUrl,
);
const entity = entityJson.entities[qid];
if (!entity) throw new Error(`missing Wikidata entity ${qid}`);

const claimIds = (property: string): string[] =>
  (entity.claims?.[property] ?? [])
    .map((claim) => entityId(claim.mainsnak?.datavalue?.value))
    .filter((value): value is string => Boolean(value));

const genreIds = claimIds("P136");
const labelIds = claimIds("P264");
const associatedIds: string[] = [];
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

if (includeRelated) {
  const specificGenreIds = genreIds
    .filter((id) => isSpecificGenre(labelMap.get(id) ?? id))
    .slice(0, 4);
  const related = await relatedArtists(
    specificGenreIds.length ? specificGenreIds : genreIds.slice(0, 2),
    qid,
  );
  for (const item of related) {
    await writeArtistNote(out, {
      name: item.name,
      wikidata: item.qid,
      musicbrainz: item.mbid,
      aliases: [],
      genres: unique(
        item.genreIds.map(
          (id) => labelMap.get(id) ?? item.factLabels.get(id) ?? id,
        ),
      ).slice(0, 4),
      labels: unique(
        item.labelIds.map(
          (id) => labelMap.get(id) ?? item.factLabels.get(id) ?? id,
        ),
      ).slice(0, 3),
      associated: [],
      sources: [`wikidata-related-${qid}`],
    });
  }
}

console.log(file);

function isSpecificGenre(value: string): boolean {
  return !/\b(?:rock music|pop music|indie rock|alternative rock|gothic rock|electronic music|popular music)\b/i.test(
    value,
  );
}

async function relatedArtists(
  factIds: string[],
  sourceQid: string,
): Promise<
  {
    qid: string;
    name: string;
    mbid?: string;
    genreIds: string[];
    labelIds: string[];
    factLabels: Map<string, string>;
  }[]
> {
  const ids = unique(factIds).filter((id) => /^Q\d+$/.test(id));
  if (!ids.length) return [];
  const values = ids.map((id) => `wd:${id}`).join(" ");
  const query = `
SELECT ?artist ?artistLabel ?mbid ?fact ?factLabel WHERE {
  VALUES ?fact { ${values} }
  { ?artist wdt:P136 ?fact . }
  UNION
  { ?artist wdt:P264 ?fact . }
  ?artist wdt:P434 ?mbid .
  FILTER(?artist != wd:${sourceQid})
  SERVICE wikibase:label { bd:serviceParam wikibase:language "en". }
}
LIMIT 12`;
  const url = `https://query.wikidata.org/sparql?format=json&query=${encodeURIComponent(query)}`;
  let json: SparqlResponse;
  try {
    json = await cachedJson<SparqlResponse>(
      cache,
      `wikidata-related-${sourceQid}-${ids.join("-")}`,
      url,
    );
  } catch {
    return [];
  }
  const byArtist = new Map<
    string,
    {
      qid: string;
      name: string;
      mbid?: string;
      genreIds: string[];
      labelIds: string[];
      factLabels: Map<string, string>;
    }
  >();
  for (const binding of json.results?.bindings ?? []) {
    const qid = binding.artist?.value.split("/").pop() ?? "";
    const fact = binding.fact?.value.split("/").pop() ?? "";
    const name = binding.artistLabel?.value ?? "";
    if (!qid || !fact || !name) continue;
    const current = byArtist.get(qid) ?? {
      qid,
      name,
      mbid: binding.mbid?.value,
      genreIds: [],
      labelIds: [],
      factLabels: new Map<string, string>(),
    };
    if (genreIds.includes(fact)) current.genreIds.push(fact);
    if (labelIds.includes(fact)) current.labelIds.push(fact);
    if (binding.factLabel?.value)
      current.factLabels.set(fact, binding.factLabel.value);
    byArtist.set(qid, current);
  }
  return [...byArtist.values()]
    .map((item) => ({
      ...item,
      genreIds: unique(item.genreIds),
      labelIds: unique(item.labelIds),
    }))
    .sort(
      (a, b) =>
        b.genreIds.length +
        b.labelIds.length * 0.5 -
        (a.genreIds.length + a.labelIds.length * 0.5),
    )
    .slice(0, 12);
}
