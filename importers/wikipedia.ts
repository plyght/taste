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
const api = "https://en.wikipedia.org/w/api.php";

type SearchResponse = { query?: { search?: { title: string }[] } };
type PageResponse = {
  query?: {
    pages?: Record<
      string,
      {
        title: string;
        categories?: { title: string }[];
        links?: { title: string }[];
        revisions?: {
          slots?: { main?: { content?: string; "*"?: string } };
          "*"?: string;
        }[];
        extract?: string;
        pageprops?: { wikibase_item?: string };
      }
    >;
  };
};

const searchUrl = `${api}?action=query&list=search&srsearch=${encodeURIComponent(artist)}&format=json&srlimit=1`;
const search = await cachedJson<SearchResponse>(
  cache,
  `wikipedia-search-${artist}`,
  searchUrl,
);
const title = search.query?.search?.[0]?.title;
if (!title) throw new Error(`no Wikipedia page found for ${artist}`);

const pageUrl = `${api}?action=query&titles=${encodeURIComponent(title)}&prop=categories|links|pageprops|revisions|extracts&rvprop=content&rvslots=main&exintro=1&explaintext=1&cllimit=200&pllimit=200&format=json`;
const pageJson = await cachedJson<PageResponse>(
  cache,
  `wikipedia-page-${title}`,
  pageUrl,
);
const page = Object.values(pageJson.query?.pages ?? {})[0];
if (!page) throw new Error(`missing Wikipedia page for ${title}`);

const revision = page.revisions?.[0];
const wikitext =
  revision?.slots?.main?.content ??
  revision?.slots?.main?.["*"] ??
  revision?.["*"] ??
  "";
const linkText = unique((page.links ?? []).map((link) => link.title));
const categories = unique(
  (page.categories ?? []).map((category) =>
    category.title.replace(/^Category:/, ""),
  ),
);

function cleanWikiValue(value: string): string {
  return value
    .replace(/<ref[\s\S]*?<\/ref>/gi, "")
    .replace(/<ref[^>]*\/>/gi, "")
    .replace(/<!--[\s\S]*?-->/g, "")
    .replace(/\{\{\s*(?:hlist|flatlist|plainlist|ubl)\s*\|/gi, "")
    .replace(/\{\{|\}\}/g, "")
    .replace(/\[\[([^|\]]+)\|([^\]]+)\]\]/g, "$2")
    .replace(/\[\[([^\]]+)\]\]/g, "$1")
    .replace(/''+/g, "")
    .replace(/&amp;/g, "&")
    .trim();
}

function splitInfoboxList(value: string): string[] {
  return unique(
    cleanWikiValue(value)
      .split(/<br\s*\/?>|\n|\||,|;/i)
      .map((item) => item.replace(/^\*+/, "").trim())
      .filter(
        (item) =>
          item &&
          !/^(yes|no|n\/a|none|hlist|flatlist|plainlist|ubl)$/i.test(item),
      ),
  ).slice(0, 16);
}

function infoboxField(names: string[]): string[] {
  for (const name of names) {
    const pattern = new RegExp(
      `\\n\\|\\s*${name}\\s*=\\s*([\\s\\S]*?)(?=\\n\\|\\s*[A-Za-z0-9_ ]+\\s*=|\\n\\}\\}|$)`,
      "i",
    );
    const match = wikitext.match(pattern);
    if (match?.[1]) return splitInfoboxList(match[1]);
  }
  return [];
}

const infoboxGenres = infoboxField(["genre", "genres"]);
const infoboxLabels = infoboxField(["label", "labels"]);
const infoboxAssociated = infoboxField([
  "associated_acts",
  "associated acts",
  "associated",
]);

const categoryGenres = categories
  .map((category) => {
    const match = category.match(
      /^(.+?) (?:musical )?(?:groups|musicians|artists|singers|bands)$/i,
    );
    return match?.[1];
  })
  .filter((value): value is string => Boolean(value))
  .filter(
    (value) =>
      !/\b(?:american|british|english|scottish|welsh|canadian|male|female|living|births|deaths|records|recordings|labels?)\b/i.test(
        value,
      ),
  )
  .slice(0, 12);

const linkLabels = unique(
  linkText.filter(
    (link) =>
      /records|recordings/i.test(link) &&
      !/identifier|discography|list of|musicbrainz|wikidata|allmusic|album|single/i.test(
        link,
      ),
  ),
).slice(0, 10);

const file = await writeArtistNote(out, {
  name: page.title,
  wikidata: page.pageprops?.wikibase_item,
  aliases: title === artist ? [] : [artist],
  genres: unique([...infoboxGenres, ...categoryGenres]),
  labels: unique([...infoboxLabels, ...linkLabels]),
  associated: infoboxAssociated,
  sources: [`wikipedia-${title.replace(/\s+/g, "_")}`],
});

console.log(file);
