# VairagyaEngine Crawler Project Information

This file is a handoff document for building a separate search engine project on top of the data produced by VairagyaEngine. It explains what the crawler does, how it stores crawled pages, which fields matter for search, and how another project can fetch relevant data based on user queries and custom filters.

## Project Summary

VairagyaEngine is a C++20 web crawler. It accepts seed URLs, fetches pages over HTTP/HTTPS, optionally extracts links for recursive crawling, parses useful page content, builds multiple metadata records, and stores everything in RocksDB.

The crawler is not a full search engine yet. Its main output is a document database containing:

- Normalized URL identity and URL hash
- Fetch status, HTTP code, latency, crawl depth, and referrer
- Parsed search content such as title, meta description, clean text, and token count
- Content hashes and simhash values for duplicate or near-duplicate detection
- Link statistics placeholders
- Quality signals such as readability, spam score, and quality score
- Presentation fields such as snippet, favicon, site name, breadcrumb, and display URL
- Control flags such as robots allowance, noindex, nofollow, and index status

A future search engine should read these stored records, build an inverted index or vector/semantic index, rank matching documents, and return search results using the presentation fields.

## Repository Layout

```text
D:\DEV\VairagyaEngine
|-- CMakeLists.txt
|-- CMakePresets.json
|-- README.md
|-- VairagyaEngine
    |-- include
    |   |-- crawler
    |   |-- host
    |   |-- html
    |   |-- net
    |   |-- pipeline
    |   |-- storage
    |   |-- url
    |   |-- utils
    |-- src
    |   |-- crawler
    |   |-- host
    |   |-- html
    |   |-- net
    |   |-- pipeline
    |   |-- storage
    |   |-- url
    |   |-- utils
    |-- external
        |-- simHash-cpp
```

Important modules:

- `src/main.cpp`: CLI entry point, argument parsing, database initialization, seed loading, and crawler startup.
- `crawler/engine.*`: Main crawl orchestration, worker threads, fetch pipeline, persistence, and link discovery.
- `crawler/frontier.*`: In-memory crawl queue, per-host queues, URL deduplication state, retry state, and pending URL snapshots.
- `crawler/scheduler.*`: Pulls the next URL from the frontier.
- `net/fetcher.*`: HTTP/HTTPS GET fetcher implemented with Boost.Beast, Boost.Asio, and OpenSSL.
- `net/response_classifier.*`: Maps HTTP status codes into OK, redirect, client error, server error, or network error classes.
- `url/normalize.*`, `url/validate.*`, `url/process.*`: URL normalization, validation, crawlability checks, priority scoring, relative URL resolution, and reversed host generation.
- `html/html_parser.*`: Extracts outgoing links from `href`, `src`, RSS/XML `loc`, and similar fields.
- `host/robots_manager.*`: Fetches/parses/caches robots.txt rules and checks whether a URL is allowed.
- `pipeline/*_builder.*`: Converts fetched content into structured records for storage and search.
- `storage/db_schema.h`: Defines the JSON-serializable record structures and RocksDB column family names.
- `storage/rocksdb_store.*`: Opens RocksDB, manages column families, reads/writes records, stores pending URLs, stores the next document id, and scans URL batches.

## Build Dependencies

The project uses CMake and requires:

- C++20 compiler
- Boost.URL
- Boost.Beast / Boost.Asio through Boost
- OpenSSL
- RocksDB
- nlohmann_json
- The bundled `external/simHash-cpp`

The executable target is `VairagyaEngine`.

## CLI Behavior

The crawler supports these main input modes:

- `-d`, `--domain`: Crawl a single URL/domain.
- `-l`, `--list`: Load seed URLs from a text file.
- `-cd`, `--crawl-database`: Load URLs from the RocksDB domain index and crawl them as seeds.
- `--resume-db`: Resume from pending URLs previously saved in RocksDB.

Useful options:

- `-cl`, `--crawl-links`: Enable link extraction and recursive crawling.
- `-sd`, `--same-domain`: Restrict discovered links to the same domain or subdomains as the seed URLs.
- `-ir`, `--ignore-robots`: Ignore robots.txt rules.
- `-db`, `--database`: RocksDB database path. Default is `vairagya_db`.
- `-t`, `--threads`: Number of crawler worker threads.
- `-oj`, `--output-json`: JSON output log path.
- `-o`, `--output`: TXT output log path.
- `-v`, `--verbose`: Verbose logging flag.

If no CLI flags are provided, `main.cpp` currently defaults to:

```text
-cd -cl -ir -t 30 -db vairagya_db
```

That means it loads URLs from the existing database, crawls recursively, ignores robots.txt, uses 30 worker threads, and uses the database path `vairagya_db`.

## Crawl Flow

High-level flow:

1. `main.cpp` parses CLI options and opens RocksDB.
2. Seed URLs are loaded from CLI, file, database scan, or pending database state.
3. Same-domain restrictions are prepared if enabled.
4. `crawler::runCrawler` creates an `Engine`.
5. Seed URLs are normalized and pushed into the `Frontier`.
6. Worker threads pop URLs from the frontier.
7. For each URL:
   - robots.txt is checked unless ignored.
   - the page is fetched with `net::fetch`.
   - the response is classified.
   - successful parseable content is converted into storage records.
   - records are persisted to RocksDB.
   - discovered links are extracted, resolved, normalized, filtered, and queued.
8. On shutdown, pending URLs are saved to RocksDB for resume support.

## URL Handling

`processURL` is the central URL intake function. It:

- Calls `normalizeURI`
- Validates the URL
- Extracts the scheme
- Rejects non-crawlable schemes
- Computes a crawl priority score

Accepted URLs must be absolute `http` or `https` URLs with a host.

Normalization behavior:

- Uses Boost.URL normalization.
- Removes URL fragments.
- Removes default port `80` for HTTP and `443` for HTTPS.
- Rejects URLs without scheme or host.

Relative link handling:

- `resolveRelativeURL(raw, base_url)` resolves relative links against the current page URL.
- Fragments-only URLs are ignored.
- `mailto:`, `javascript:`, and `tel:` links are ignored.
- Only `http` and `https` resolved URLs are allowed.

Priority scoring:

- Base score is `50`.
- Deeper paths reduce score.
- Root-ish URLs, `.html`, and trailing slash URLs get a boost.
- Static assets such as `.css`, `.js`, `.png`, `.jpg`, and `.svg` are penalized.
- Score is clamped to `0..100`.

Domain index support:

- `reverseHost(url)` converts a host such as `www.example.com` to `com.example.www`.
- RocksDB domain index keys use reversed host ordering for domain-oriented scans.

## Fetching

`net::fetch(url, timeout_ms = 5000)` performs a GET request.

Fetch behavior:

- Uses Boost.Beast for HTTP.
- Uses OpenSSL for HTTPS.
- Sets user agent to `VairagyaEngine/1.0`.
- Uses SNI for HTTPS.
- Default timeout is 5000 ms.
- SSL verification is currently disabled with `ssl::verify_none`, crawler-style.
- Returns the response body, HTTP code, latency, and content type.

`FetchResult` fields:

```cpp
FetchStatus status;
string content;
uint16_t http_code;
long long fetch_time_ms;
string content_type;
```

Response classification:

- HTTP `2xx` -> `OK`
- HTTP `3xx` -> `REDIRECT`
- HTTP `4xx` -> `CLIENT_ERROR`
- HTTP `5xx` -> `SERVER_ERROR`
- HTTP code `0` -> `NETWORK_ERROR`

Redirects are currently treated as terminal fetched results. The fetcher does not currently follow redirects.

## Parseable Content Rules

For HTTP 200 responses, the crawler decides whether content is parseable.

Parseable content types:

- `text/html`
- `text/plain`
- `application/rss+xml`
- `application/xml`
- `application/xhtml+xml`
- empty content type, with fallback URL extension checks

Excluded URL patterns include:

- `.woff`
- `.woff2`
- `.ttf`
- `.gif`
- `.ico`
- `.zip`
- `.gz`
- `.bin`
- `.js`
- `.css`

Only parseable content is passed into the text parsing and link extraction pipeline.

## Link Extraction

`extractLinks(content)` uses a regex-based parser to extract:

- `href="..."`
- `src="..."`
- `loc="..."`
- XML/RSS-style `<link>...</link>`
- XML/RSS-style `<loc>...</loc>`

Discovered links are:

1. Trimmed.
2. Resolved relative to the current page.
3. Normalized and validated with `processURL`.
4. Checked against same-domain restrictions if enabled.
5. Checked against robots rules unless robots are ignored.
6. Filtered through `isHtmlPageUrl`.
7. Added back to the frontier with increased crawl depth and the current URL as referrer.

## Robots.txt Handling

Robots behavior lives in `host/robots_manager.*`.

When `ignore_robots` is false:

- The crawler builds the robots URL as `{origin}/robots.txt`.
- It fetches robots.txt once per host and caches rules in memory.
- It parses `User-agent`, `Allow`, `Disallow`, and `Sitemap` lines.
- It applies rules for `VairagyaEngine` and `*`.
- It uses longest matching rule behavior where allow wins if it is at least as specific as disallow.
- Wildcards `*` and end anchors `$` are supported in matching.

When `ignore_robots` is true:

- robots.txt fetching and blocking are skipped.

Important for the future search engine:

- `ControlFlags.robots_allowed` exists in the schema.
- Current `ControlFlagsBuilder::build` is called with `true` for successfully indexed pages.
- If strict robots/noindex enforcement is required in the search engine, verify and enforce `noindex`, `nofollow`, and robots decisions before serving documents.

## Frontier, Retries, and Resume

The frontier is currently in-memory while the crawler runs.

It tracks:

- Per-host queues
- URL state by normalized URL
- Fetch status
- HTTP status
- Retry count
- Last fetch timestamp
- Crawl stats

Retry behavior:

- `MAX_RETRY_COUNT` is `3`.
- Server errors and network errors are retryable.
- Client errors are terminal failures.
- Retry entries are pushed back to the frontier.

Resume behavior:

- `RocksDBStore::savePendingURLs` stores a JSON array under key `__pending_urls__` in the default column family.
- `RocksDBStore::loadPendingURLs` reads that JSON array.
- `--resume-db` uses these pending URLs as seeds.

## RocksDB Storage Model

The crawler stores records in RocksDB column families. The primary join key for document records is:

```text
doc_core.url_hash = sha256(normalized_url)
```

Most column families use `url_hash` as the key and a JSON object as the value.

Column families:

| Column family | Key | Value | Purpose |
|---|---|---|---|
| `default` | internal keys | strings or JSON | `next_doc_id`, scan cursor, pending URLs |
| `doc_core_cf` | `url_hash` | `DocCore` JSON | identity and URL metadata |
| `domain_index_cf` | `d:{reversed_host}|/|{doc_id}` | `url_hash` | domain-ordered scan index |
| `fetch_meta_cf` | `url_hash` | `FetchMeta` JSON | fetch metadata |
| `content_meta_cf` | `url_hash` | `ContentMeta` JSON | hashes and duplicate signals |
| `parsed_content_cf` | `url_hash` | `ParsedContent` JSON | title, description, clean text |
| `link_graph_cf` | `url_hash` | `LinkData` JSON | link metrics |
| `quality_cf` | `url_hash` | `QualitySignals` JSON | quality and spam signals |
| `presentation_cf` | `url_hash` | `Presentation` JSON | snippet and display fields |
| `control_cf` | `url_hash` | `ControlFlags` JSON | noindex/nofollow/index state |

Internal default column family keys:

- `next_doc_id`: next numeric document id to assign.
- `__pending_urls__`: JSON array of pending URLs for resume.
- `__scan_cursor__`: cursor used by `getUrlsBatch` when scanning `domain_index_cf`.

## Stored Record Schemas

### DocCore

Stored in `doc_core_cf`.

```json
{
  "doc_id": 1,
  "normalized_url": "https://example.com/page",
  "url_hash": "sha256(normalized_url)",
  "canonical_url": "https://example.com/canonical",
  "first_seen_time": 1710000000,
  "language_code": "en",
  "charset": "UTF-8",
  "content_type": "text/html"
}
```

Fields:

- `doc_id`: Internal numeric id. Assigned by `RocksDBStore::getNextDocId`.
- `normalized_url`: Canonical crawler URL after normalization.
- `url_hash`: SHA-256 of normalized URL. Main key across column families.
- `canonical_url`: Extracted from HTML `<link rel="canonical" href="...">` if present.
- `first_seen_time`: Unix timestamp.
- `language_code`: Extracted from `<html lang="...">`; defaults to `en`.
- `charset`: Extracted from meta charset; defaults to `UTF-8`.
- `content_type`: Extracted from meta content type; defaults to `text/html`.

Search engine use:

- Use `doc_id` as compact internal id.
- Use `url_hash` to join records.
- Use `normalized_url` as the final clickable URL unless `canonical_url` rules say otherwise.
- Use `language_code` for language filtering or language-specific analyzers.

### FetchMeta

Stored in `fetch_meta_cf`.

```json
{
  "last_fetched_time": 1710000000,
  "fetch_status_code": 200,
  "fetch_latency_ms": 123,
  "content_length_bytes": 45678,
  "etag": "",
  "last_modified": "",
  "crawl_depth": 1,
  "referrer_url": "https://example.com/",
  "crawl_priority": 0
}
```

Search engine use:

- Filter to successful documents with `fetch_status_code == 200`.
- Use `last_fetched_time` for freshness ranking.
- Use `crawl_depth` as a weak quality/trust feature.
- Use `content_length_bytes` to filter empty or extremely small documents.

### ContentMeta

Stored in `content_meta_cf`.

```json
{
  "content_hash": "sha256(clean_text)",
  "simhash": 123456789,
  "is_duplicate": false,
  "canonical_doc_id": 0
}
```

Search engine use:

- Use `content_hash` for exact duplicate detection.
- Use `simhash` for near-duplicate clustering.
- Prefer one canonical document per duplicate cluster.
- `is_duplicate` and `canonical_doc_id` are currently basic fields and need stronger duplicate resolution if search quality matters.

### ParsedContent

Stored in `parsed_content_cf`. This is the most important search input.

```json
{
  "title": "Example Page Title",
  "meta_description": "Short page description",
  "clean_text": "Visible page text after basic tag removal",
  "token_count": 350
}
```

Current parser behavior:

- Extracts `<title>...</title>`.
- Extracts `<meta name="description" content="...">`.
- Removes `<script>` and `<style>` blocks.
- Removes HTML tags.
- Normalizes whitespace.
- Counts tokens approximately using whitespace count plus one.

Search engine use:

- Index `title` with high weight.
- Index `meta_description` with medium weight.
- Index `clean_text` as the main body.
- Use `token_count` to suppress thin pages or normalize ranking.

Recommended weighting for a simple lexical search:

```text
title: 4.0
meta_description: 2.0
clean_text/body: 1.0
URL/domain match: 0.5 to 1.5
freshness/quality boost: 0.1 to 1.0
```

### LinkData

Stored in `link_graph_cf`.

```json
{
  "outbound_links_count": 42,
  "inbound_links_count": 0,
  "pagerank_score": 0.0
}
```

Current behavior:

- `outbound_links_count` is populated from extracted links.
- `inbound_links_count` is currently not fully computed.
- `pagerank_score` is currently a placeholder.

Search engine use:

- Use `outbound_links_count` carefully as a weak feature.
- Build a separate link graph processor later if PageRank or authority ranking is needed.
- To support real PageRank, persist actual edges, not only counts.

### QualitySignals

Stored in `quality_cf`.

```json
{
  "content_last_changed_time": 1710000000,
  "update_frequency": 1.0,
  "spam_score": 0.1,
  "quality_score": 0.81,
  "readability_score": 0.9
}
```

Current behavior:

- `readability_score` is a simple approximation.
- `spam_score` is basic: short content gets high spam score.
- `quality_score = (1.0 - spam_score) * readability_score`.
- `update_frequency` is a placeholder.

Search engine use:

- Use as secondary ranking boosts, not the main ranker.
- Consider excluding pages with high spam score or very low quality score.
- Improve these calculations in a later indexing pipeline.

### Presentation

Stored in `presentation_cf`. This is useful for search result rendering.

```json
{
  "snippet": "Beginning of clean page text...",
  "favicon_url": "/favicon.ico",
  "site_name": "",
  "breadcrumb": "",
  "display_url": "https://example.com/page"
}
```

Current behavior:

- `snippet` is the first part of clean text, max 160 characters by default.
- `favicon_url` is extracted from `<link rel="icon"...>` or defaults to `/favicon.ico`.
- `display_url` is set to the normalized URL.
- `site_name` and `breadcrumb` are currently passed as empty strings.

Search engine use:

- Use this record to render title/snippet/display URL results.
- For better result snippets, generate query-aware snippets in the search engine instead of always using this static snippet.
- Resolve relative favicon URLs against the page origin before serving them.

### ControlFlags

Stored in `control_cf`.

```json
{
  "robots_allowed": true,
  "noindex": false,
  "nofollow": false,
  "index_status": "PENDING",
  "error_reason": ""
}
```

Current behavior:

- Detects `<meta name="robots" content="...noindex...">`.
- Detects `<meta name="robots" content="...nofollow...">`.
- Sets `index_status` to `SKIPPED_NOINDEX` if noindex is detected, otherwise `PENDING`.

Search engine use:

- Do not serve documents with `noindex == true`.
- Consider not following links from pages with `nofollow == true` in future crawler versions.
- Treat `index_status` as a lifecycle field. A separate indexer can set statuses such as `INDEXED`, `SKIPPED_DUPLICATE`, `SKIPPED_NOINDEX`, or `FAILED_PARSE`.

## Reading Data From Another Project

The future search engine can consume RocksDB directly.

Recommended read pattern:

1. Iterate `domain_index_cf` keys starting with `d:`.
2. Read the value, which is the `url_hash`.
3. Fetch related records by `url_hash` from:
   - `doc_core_cf`
   - `fetch_meta_cf`
   - `parsed_content_cf`
   - `content_meta_cf`
   - `quality_cf`
   - `presentation_cf`
   - `control_cf`
4. Skip documents that should not be searchable.
5. Build a search index from the accepted documents.

Minimum fields needed for a usable search engine:

- From `doc_core_cf`: `doc_id`, `url_hash`, `normalized_url`, `canonical_url`, `language_code`
- From `fetch_meta_cf`: `fetch_status_code`, `last_fetched_time`, `crawl_depth`
- From `parsed_content_cf`: `title`, `meta_description`, `clean_text`, `token_count`
- From `quality_cf`: `quality_score`, `spam_score`
- From `presentation_cf`: `snippet`, `favicon_url`, `display_url`
- From `control_cf`: `noindex`, `index_status`

Basic skip rules:

```text
skip if fetch_status_code != 200
skip if noindex == true
skip if clean_text is empty
skip if token_count is too low for your product needs
skip or canonicalize if duplicate/canonical_doc_id indicates a duplicate
```

## Suggested Search Engine Architecture

A separate search project can be built in stages:

1. RocksDB importer
   - Reads VairagyaEngine column families.
   - Joins records by `url_hash`.
   - Emits one complete document object per page.

2. Document cleaner
   - Applies skip rules.
   - Handles canonical URLs and duplicate clusters.
   - Normalizes language, title, body, and display URL.

3. Index builder
   - Tokenizes `title`, `meta_description`, and `clean_text`.
   - Builds an inverted index for keyword search.
   - Optionally builds embeddings for semantic search.
   - Stores document metadata separately from postings.

4. Query processor
   - Parses user query.
   - Normalizes terms.
   - Applies filters and customization options.
   - Retrieves candidate documents.

5. Ranker
   - Scores candidates using text relevance and metadata boosts.
   - Applies quality, freshness, duplicate, and domain diversity rules.

6. Result renderer
   - Produces title, URL, snippet, favicon, and metadata.
   - Generates query-aware snippets from `clean_text`.

## Search Relevance Inputs

Useful ranking features from current crawler output:

- Exact query term matches in `title`
- Query term matches in `meta_description`
- Query term matches in `clean_text`
- Phrase match in title/body
- URL/domain match
- Freshness from `last_fetched_time`
- Quality boost from `quality_score`
- Spam penalty from `spam_score`
- Thin-content penalty from `token_count`
- Crawl-depth penalty or boost
- Duplicate suppression using `content_hash` and `simhash`
- Language match using `language_code`

Simple ranking formula idea:

```text
score =
  4.0 * title_bm25 +
  2.0 * description_bm25 +
  1.0 * body_bm25 +
  0.5 * url_match_score +
  0.5 * quality_score -
  1.0 * spam_score +
  freshness_boost -
  duplicate_penalty
```

For a first version, BM25 over `title`, `meta_description`, and `clean_text` is enough.

For a more advanced version, combine:

- BM25 lexical retrieval
- Semantic/vector retrieval
- Query-aware snippet generation
- Domain diversity
- User personalization or custom filters

## Customization Options For User Search

The future search engine can expose filters and ranking customizations using crawler data.

Possible filters:

- Domain/site filter: use `normalized_url` or domain parsed from URL.
- Language filter: use `DocCore.language_code`.
- Freshness filter: use `FetchMeta.last_fetched_time`.
- Content length filter: use `ParsedContent.token_count` or `FetchMeta.content_length_bytes`.
- Quality filter: use `QualitySignals.quality_score`.
- Spam-safe mode: exclude high `spam_score`.
- Exact URL lookup: hash normalized URL with SHA-256 and read directly by `url_hash`.
- Duplicate filtering: use `ContentMeta.content_hash`, `simhash`, `is_duplicate`, and `canonical_doc_id`.
- Safe indexing filter: exclude `ControlFlags.noindex == true`.

Possible ranking controls:

- Prefer recent pages.
- Prefer high quality pages.
- Prefer a specific domain.
- Prefer title matches.
- Prefer longer/deeper content.
- Prefer same-language results.
- Collapse duplicate results.

## Query Result Shape

A good result object for the new search engine:

```json
{
  "doc_id": 1,
  "url_hash": "sha256(normalized_url)",
  "title": "Example Page Title",
  "url": "https://example.com/page",
  "display_url": "example.com/page",
  "snippet": "Query-aware snippet with highlighted terms...",
  "favicon_url": "https://example.com/favicon.ico",
  "language": "en",
  "score": 12.34,
  "last_fetched_time": 1710000000,
  "quality_score": 0.81
}
```

The crawler already provides most of these fields, but query-aware snippets and final relevance score should be generated by the search project.

## Important Limitations In Current Crawler

These are important when building the next project:

- The crawler stores parsed records, not a ready-to-query inverted index.
- Link graph storage only keeps counts, not actual edge lists.
- PageRank is currently a placeholder.
- Inbound link count is currently not fully computed.
- The HTML parser is regex-based and basic.
- Text extraction does not decode HTML entities or remove all boilerplate/navigation content.
- Redirects are not followed.
- SSL verification is disabled in the fetcher.
- ETag and Last-Modified are stored in the schema but currently passed as empty strings.
- `site_name` and `breadcrumb` are currently empty.
- `quality_score`, `spam_score`, and `readability_score` are simple approximations.
- `noindex` is detected, but the crawler still writes records; the search engine should enforce exclusion.
- Pending URL resume stores only URLs, not full frontier priority/depth/referrer state.
- The database schema is append/update by URL hash; there is no separate version history.

## Recommended Improvements Before Large-Scale Search

Crawler-side improvements:

- Follow redirects and store final URL.
- Store actual link edges: `source_url_hash -> target_url_hash`.
- Compute inbound link counts and PageRank in a separate graph job.
- Improve HTML parsing with a real parser.
- Decode HTML entities.
- Extract Open Graph fields, schema.org metadata, and site name.
- Generate better canonical URL handling.
- Store resolved favicon URLs.
- Respect noindex before marking pages indexable.
- Store robots decision accurately in `ControlFlags`.
- Persist full frontier state for exact resume.
- Add per-host rate limiting and crawl-delay enforcement.
- Add content size limits to avoid very large responses.

Search-side improvements:

- Build a durable inverted index instead of scanning RocksDB at query time.
- Use BM25 or another proven lexical ranker.
- Add stemming/lemmatization depending on language.
- Add stopword handling.
- Add typo tolerance or fuzzy matching.
- Add semantic embeddings if needed.
- Generate query-aware snippets.
- Collapse duplicates using content hash/simhash.
- Add domain diversity.
- Add freshness and quality boosts.

## Minimal Importer Pseudocode

```text
open RocksDB with the same column families

for each key/value in domain_index_cf where key starts with "d:":
    url_hash = value

    doc_core = json_get(doc_core_cf, url_hash)
    fetch_meta = json_get(fetch_meta_cf, url_hash)
    parsed = json_get(parsed_content_cf, url_hash)
    content_meta = json_get(content_meta_cf, url_hash)
    quality = json_get(quality_cf, url_hash)
    presentation = json_get(presentation_cf, url_hash)
    control = json_get(control_cf, url_hash)

    if fetch_meta.fetch_status_code != 200:
        continue
    if control.noindex:
        continue
    if parsed.clean_text is empty:
        continue

    document = {
        id: doc_core.doc_id,
        url_hash: doc_core.url_hash,
        url: doc_core.normalized_url,
        canonical_url: doc_core.canonical_url,
        language: doc_core.language_code,
        title: parsed.title,
        description: parsed.meta_description,
        body: parsed.clean_text,
        token_count: parsed.token_count,
        snippet: presentation.snippet,
        favicon_url: presentation.favicon_url,
        quality_score: quality.quality_score,
        spam_score: quality.spam_score,
        last_fetched_time: fetch_meta.last_fetched_time,
        simhash: content_meta.simhash
    }

    add document to search index
```

## Practical First Version Plan For The Search Engine

For the first working version:

1. Create a RocksDB reader that opens the same database path and column families.
2. Convert each crawled page into a single document object.
3. Skip failed, noindex, empty, duplicate, or low-quality documents.
4. Build an inverted index over title, description, and body.
5. Implement BM25 scoring with separate field weights.
6. Return title, URL, snippet, favicon, score, and timestamp.
7. Add filters for domain, language, freshness, and quality.
8. Generate snippets based on query term positions in `clean_text`.

This will use the crawler's existing data without requiring changes to VairagyaEngine.

## Files To Study When Integrating

- `VairagyaEngine/include/storage/db_schema.h`
- `VairagyaEngine/include/storage/rocksdb_store.h`
- `VairagyaEngine/src/storage/rocksdb_store.cpp`
- `VairagyaEngine/src/crawler/engine.cpp`
- `VairagyaEngine/src/pipeline/parsed_content_builder.cpp`
- `VairagyaEngine/src/pipeline/presentation_builder.cpp`
- `VairagyaEngine/src/pipeline/content_meta_builder.cpp`
- `VairagyaEngine/src/pipeline/quality_signals_builder.cpp`
- `VairagyaEngine/src/url/process.cpp`

## Key Takeaway

VairagyaEngine is best treated as the crawling and document-preparation layer. The future search engine should treat RocksDB as the source of crawled documents, join records by `url_hash`, enforce indexability rules, build its own optimized search index, and use the crawler's metadata as ranking, filtering, duplicate-detection, and result-presentation signals.
