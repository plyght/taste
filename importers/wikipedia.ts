import { cachedJson, cacheDir, outDir, requireArtist, unique, writeArtistNote } from "./common";

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
        pageprops?: { wikibase_item?: string };
      }
    >;
  };
};

const searchUrl = `${api}?action=query&list=search&srsearch=${encodeURIComponent(artist)}&format=json&srlimit=1`;
const search = await cachedJson<SearchResponse>(cache, `wikipedia-search-${artist}`, searchUrl);
const title = search.query?.search?.[0]?.title;
if (!title) throw new Error(`no Wikipedia page found for ${artist}`);

const pageUrl = `${api}?action=query&titles=${encodeURIComponent(title)}&prop=categories|links|pageprops&cllimit=100&pllimit=100&format=json`;
const pageJson = await cachedJson<PageResponse>(cache, `wikipedia-page-${title}`, pageUrl);
const page = Object.values(pageJson.query?.pages ?? {})[0];
if (!page) throw new Error(`missing Wikipedia page for ${title}`);

const categoryText = unique((page.categories ?? []).map((category) => category.title.replace(/^Category:/, "")));
const linkText = unique((page.links ?? []).map((link) => link.title));
const genres = categoryText
  .filter((category) => /music|rock|pop|metal|electronic|ambient|shoegaze|punk|folk|jazz|hip hop|wave|indie/i.test(category))
  .map((category) => category.replace(/ musicians$| groups$| bands$/i, ""))
  .slice(0, 20);
const associated = linkText
  .filter((link) => !/List of|Discography|Records|MusicBrainz|Wikidata|ISBN/i.test(link))
  .slice(0, 20);
const file = await writeArtistNote(out, {
  name: page.title,
  wikidata: page.pageprops?.wikibase_item,
  aliases: title === artist ? [] : [artist],
  genres: unique(genres),
  labels: unique(linkText.filter((link) => /Records|Recordings|Entertainment|Music$/i.test(link)).slice(0, 10)),
  associated: unique(associated),
  sources: [`wikipedia-${title.replace(/\s+/g, "_")}`],
});

console.log(file);
