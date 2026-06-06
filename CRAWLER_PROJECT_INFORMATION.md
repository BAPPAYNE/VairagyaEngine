# VairagyaEngine Project Information — Latest Handoff

> Updated from the uploaded `VairagyaEngine.7z` project snapshot and the current crawler/search-engine design discussion.

## 1. Project Summary

VairagyaEngine is a C++20 crawler plus lightweight local search API. It is no longer only a crawler/document-preparation project. The current codebase has two runtime modes:

```text
--mode crawler  -> crawl/fetch/parse/store pages into RocksDB
--mode api      -> load stored documents, build an in-memory search index, expose HTTP search endpoints
```

The crawler layer fetches HTTP/HTTPS pages, checks robots rules unless disabled, parses useful content, extracts links, stores document records into RocksDB, records recrawl state, and saves pending frontier URLs for resume.

The search layer reads RocksDB records, builds an in-memory inverted index, applies query processing, fuzzy token expansion, BM25-style ranking, snippet generation, and click tracking.

Current important point: VairagyaEngine is still not a production-scale Google-like distributed engine. It is a single-machine crawler + RocksDB document store + in-memory API search engine.

---

## 2. Current Repository Layout

```text
VairagyaEngine
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── CRAWLER_PROJECT_INFORMATION.md
└── VairagyaEngine
    ├── config
    │   └── synonyms.json
    ├── external
    │   ├── simHash-cpp
    │   └── tsl
    ├── include
    │   ├── api
    │   ├── crawler
    │   ├── host
    │   ├── html
    │   ├── net
    │   ├── pipeline
    │   ├── query
    │   ├── storage
    │   ├── url
    │   └── utils
    └── src
        ├── api
        ├── crawler
        ├── host
        ├── html
        ├── net
        ├── pipeline
        ├── query
        ├── storage
        ├── url
        └── utils
```

Important files:

| Area | Files | Purpose |
|---|---|---|
| CLI/runtime | `src/main.cpp` | argument parsing, mode selection, DB open, seed loading |
| Crawler engine | `crawler/engine.*` | worker threads, robots check, fetch, parse, store, link enqueue |
| Frontier | `crawler/frontier.*` | per-host priority queues, URL states, retry state, pending snapshot |
| Scheduler | `crawler/scheduler.*` | wrapper around frontier `popWait` |
| Networking | `net/fetcher.*` | Boost.Beast HTTP/HTTPS GET fetcher |
| Response classification | `net/response_classifier.*` | maps status code to OK/REDIRECT/CLIENT_ERROR/SERVER_ERROR/NETWORK_ERROR |
| Robots | `host/robots_manager.*` | robots.txt parsing, allow/disallow matching, sitemap line parsing into rules |
| HTML/link parsing | `html/html_parser.*` | regex link extraction from `href`, `src`, `loc`, `<link>`, `<loc>` |
| URL processing | `url/normalize.*`, `url/validate.*`, `url/process.*` | normalization, validation, priority, relative resolution, reversed host |
| Storage schema | `storage/db_schema.h` | JSON-serializable crawler DB records |
| RocksDB storage | `storage/rocksdb_store.*` | column families, pending URLs, recrawl state, document loading, clicks |
| Pipeline builders | `pipeline/*_builder.*` | builds `DocCore`, `ParsedContent`, `FetchMeta`, etc. |
| Query/search | `query/*` | tokenizer, inverted index, ranker, snippets, result builder, query engine |
| API | `api/search_api.*` | Crow HTTP API: `/search`, `/health`, `/stats`, `/click` |

---

## 3. Dependencies and Build

The project target is `VairagyaEngine` and uses C++20.

Required libraries:

```text
Boost.URL
Boost.Beast / Boost.Asio
OpenSSL
RocksDB
nlohmann_json
Crow
```

Bundled libraries:

```text
external/simHash-cpp
external/tsl/robin_map / robin_set
include/utils/argparse.hpp
```

`CMakeLists.txt` currently finds:

```cmake
find_package(boost_url CONFIG REQUIRED)
find_package(RocksDB CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)
```

Crow is included as `<crow.h>`, so Crow headers must be available to the compiler include path.

Build target sources include crawler, storage, pipeline, query, API, and simhash modules.

---

## 4. Runtime Modes

### 4.1 Crawler Mode

```bash
VairagyaEngine.exe --mode crawler -d https://cplusplus.com/ -db vairagya_db -sd -cl -t 2
```

Common crawler flags:

| Flag | Meaning |
|---|---|
| `-d`, `--domain` | one seed URL |
| `-l`, `--list` | seed URL list file |
| `-cd`, `--crawl-database` | select due recrawl URLs from RocksDB |
| `--resume-db` | load pending URLs from `__pending_urls__` |
| `--remove-duplicates` | cleanup duplicate URL/domain/pending records then exit |
| `-cl`, `--crawl-links` | enable recursive link extraction |
| `-sd`, `--same-domain` | restrict discovered links to seed domains/subdomains |
| `-ir`, `--ignore-robots` | skip robots.txt enforcement |
| `-db`, `--database` | RocksDB path |
| `-t`, `--threads` | worker thread count |
| `-oj`, `--output-json` | JSON URL log output path |
| `-o`, `--output` | TXT URL log output path |

Current default args when no CLI flags are provided:

```text
--mode crawler -d https://cplusplus.com/ -db vairagya_db -t 5 -sd -cl
```

### 4.2 API Mode

```bash
VairagyaEngine.exe --mode api -db vairagya_db --port 8080
```

API mode opens the RocksDB database, loads searchable documents into an in-memory index, and starts Crow.

Endpoints:

| Endpoint | Method | Purpose |
|---|---|---|
| `/search?q=...&page=1&limit=10` | GET | query search index |
| `/health` | GET | health + searchable document count |
| `/stats` | GET | basic document/cache stats |
| `/click?doc_id=123` | GET/POST | increments click count for a result |

Example result object:

```json
{
  "doc_id": 1,
  "title": "Example Page",
  "url": "https://example.com/page",
  "display_url": "https://example.com/page",
  "favicon_url": "/favicon.ico",
  "language": "en",
  "snippet": "Query-aware snippet...",
  "score": 12.34,
  "last_fetched_time": 1710000000,
  "quality_score": 0.81
}
```

---

## 5. Current Crawl Flow

Crawler flow in `Engine::processItem`:

```text
FrontierItem
  ↓
robots.txt check unless ignore_robots=true
  ↓
net::fetch(url)
  ↓
net::classify(result)
  ↓
OK / REDIRECT / CLIENT_ERROR / SERVER_ERROR / NETWORK_ERROR handling
  ↓
For HTTP 200 parseable content:
    build DocCore
    assign/reuse doc_id
    build ParsedContent / FetchMeta / ContentMeta / LinkData / QualitySignals / Presentation / ControlFlags
    write all records into RocksDB
    record recrawl state
    extract links if -cl enabled
    resolve + process + same-domain check + robots check + enqueue
  ↓
mark fetched / failed / retry / disallowed in Frontier
```

Important behavior:

- Only `HTTP 200` enters the document-building pipeline.
- Redirects are currently terminal; they are recorded but not followed.
- `4xx` responses are currently terminal failures.
- `5xx` and network errors are retryable.
- `429` is currently treated as `CLIENT_ERROR` because it is `4xx`; this should be changed to host-level throttling/backoff later.
- Worker threads use `frontier.popWait(g_running)` and stop when no queued URLs and no active workers remain.

---

## 6. URL Processing

Current archive code has:

```cpp
struct ProcessedURL {
    string original;
    string normalized;
    URLStatus status;
    int priority;
    SchemeType scheme;
    Crawlability crawlability;
};
```

Your latest intended design adds:

```cpp
ResourceType resource_type;
```

Recommended latest struct:

```cpp
struct ProcessedURL {
    string original = "";
    string normalized = "";
    URLStatus status = URLStatus::INVALID_URL;
    int priority = 0;
    SchemeType scheme = SchemeType::NONE;
    Crawlability crawlability = Crawlability::NON_CRAWLABLE;
    ResourceType resource_type = ResourceType::UNKNOWN;
};
```

Current URL rules:

- URL must normalize successfully.
- URL must be absolute.
- Scheme must be `http` or `https`.
- `mailto:`, `javascript:`, `tel:` are rejected during relative resolution.
- Fragments are removed during relative resolution.
- `priorityScore()` returns `0..100`.

Important current design decision:

```text
priority == 0 does not necessarily mean invalid.
It can mean lowest priority.
A URL should be skipped based on status/crawlability/resource type, not only priority.
```

Recommended resource classification:

| ResourceType | Fetch body? | Index as web text? | Notes |
|---|---:|---:|---|
| `HTML_PAGE` | yes | yes | normal pages |
| `TEXT_DOCUMENT` | yes | yes | `.txt`, `.md`, `.rst`, RFC txt files |
| `SITEMAP_XML` | yes | no | parse `<loc>` and enqueue URLs |
| `PDF_DOCUMENT` | later | later | needs PDF text extraction |
| `IMAGE` | no body | no | store metadata from HTML later |
| `VIDEO` | no body | no | store metadata from HTML later |
| `AUDIO` | no body | no | store metadata from HTML later |
| `STATIC_ASSET` | no | no | CSS/JS/fonts/archives/executables |

Recommended `shouldFetchBody`:

```cpp
bool shouldFetchBody(ResourceType type) {
    return type == ResourceType::HTML_PAGE ||
           type == ResourceType::TEXT_DOCUMENT ||
           type == ResourceType::SITEMAP_XML;
}
```

---

## 7. Frontier and Scheduling

`Frontier` currently uses per-host queues:

```cpp
unordered_map<string, HostState> hostQueue;
```

Each host has:

```cpp
priority_queue<FrontierItem, vector<FrontierItem>, FrontierItemPriority> urlQueue;
```

`FrontierItem`:

```cpp
struct FrontierItem {
    string normalized_url;
    int priority;
    uint8_t retry_count;
    int depth;
    string referrer_url;
};
```

Priority comparator:

```cpp
return left.priority < right.priority;
```

That means higher priority values are popped first inside a host queue.

Current limitations:

- Host selection loops through an `unordered_map`, so host-level fairness is not deterministic.
- There is no active host-level cooldown/429 pause system yet.
- `rate_limit.h`, `rate_limit.cpp`, `resolver.h`, and `resolver.cpp` are currently empty in the uploaded archive.
- Retry puts failed URL back into the host queue until `MAX_RETRY_COUNT`.
- Pending resume stores only URLs, not priority/depth/referrer/retry count.

Recommended next frontier design for 429:

```text
ready_hosts queue
paused_hosts_429 min-heap by wake_time
host_queues map<host, priority_queue<FrontierItem>>
```

Do not move all URLs from host queue to a retry URL queue. Pause the host and keep its URLs in its per-host queue.

---

## 8. Robots.txt Handling

`RobotsManager` currently:

- Builds robots URL as `{origin}/robots.txt`.
- Fetches robots lazily in `Engine::processItem` if host rules are not cached.
- Parses `User-agent`, `Allow`, `Disallow`, and `Sitemap`.
- Keeps specific-agent rules separate from wildcard rules.
- Specific crawler group overrides wildcard group if present.
- Uses longest-match behavior; allow wins when allow length is at least disallow length.
- Supports `*` wildcard and trailing `$` anchor in rule matching.

Current crawler behavior:

```text
if ignore_robots=false:
    fetch robots.txt once per host
    cache rules in memory
    block URL if canFetch(url) is false
else:
    do not fetch/check robots
```

Current limitation:

- `Sitemap:` lines are parsed into `RobotsRules.sitemaps`, but the crawler does not yet automatically enqueue them.
- `crawl-delay` is not parsed/enforced.
- Robots decisions are not persisted as full per-URL policy history.

Recommended improvement:

```text
After robots fetch:
  for each rules.sitemaps:
      addURL(sitemap_url, 0, "ROBOTS_SITEMAP")
```

---

## 9. Fetching

`net::fetch(url, timeout_ms = 5000)` uses Boost.Beast.

Behavior:

- Supports HTTP and HTTPS.
- Uses OpenSSL for HTTPS.
- Sets SNI for HTTPS.
- Uses `VairagyaEngine/1.0` user agent.
- Default timeout: 5000 ms.
- SSL verification is currently disabled: `ssl::verify_none`.
- Uses system DNS resolver through Boost.Asio.

Current `FetchResult`:

```cpp
struct FetchResult {
    FetchStatus status;
    string content;
    uint16_t http_code;
    long long fetch_time_ms;
    string content_type;
};
```

Recommended additions:

```cpp
long long retry_after_ms = 0;
string redirect_url = "";
```

Why:

- `retry_after_ms` is needed for `429 Too Many Requests`.
- `redirect_url` is needed to follow `Location` on `3xx` responses.

---

## 10. Response Classification

Current behavior:

```text
2xx -> OK
3xx -> REDIRECT
4xx -> CLIENT_ERROR
5xx -> SERVER_ERROR
0   -> NETWORK_ERROR
```

Crawler handling:

| Class | Current handling |
|---|---|
| OK + 200 | parse/store/index/extract links |
| REDIRECT | terminal record, not followed |
| CLIENT_ERROR | terminal failed |
| SERVER_ERROR | retryable |
| NETWORK_ERROR | retryable |

Recommended changes:

- Treat `429` specially, not like normal `4xx`.
- Parse `Retry-After` header.
- Pause only the host that returned `429`.
- Follow redirects up to a max redirect count.
- Store redirect aliases and canonical target.

---

## 11. Parseable Content and Resource Types

Current archive code decides parseability after fetching by checking content type and some file extensions.

Parseable content types currently include:

```text
text/html
text/plain
application/rss+xml
application/xml
application/xhtml+xml
empty content-type fallback
```

Excluded by URL extension:

```text
.woff, .woff2, .ttf, .gif, .ico, .zip, .gz, .bin, .js, .css
```

Recommended latest behavior:

```text
HTML_PAGE      -> parse HTML, index text, extract links/media
TEXT_DOCUMENT  -> text parser, index text, no HTML parsing
SITEMAP_XML    -> parse <loc>, enqueue URLs, do not index sitemap itself
IMAGE/VIDEO/AUDIO -> do not download body; store metadata later from HTML
STATIC_ASSET   -> skip
```

Important example:

```text
https://www.rfc-editor.org/rfc/rfc1264.txt
```

This should be treated as `TEXT_DOCUMENT`, fetched, stored as clean text, and indexed. It is not a static asset.

Important sitemap example:

```text
https://www.netacad.com/sitemap.xml
```

This should be treated as `SITEMAP_XML`, fetched, parsed for `<loc>` URLs, and not indexed as a normal searchable page.

---

## 12. Link Extraction

Current `extractLinks(content)` uses one regex to extract:

```text
href="..."
src="..."
loc="..."
<link>...</link>
<loc>...</loc>
```

Current link pipeline:

```text
raw link
  ↓
resolveRelativeURL(raw, current_url)
  ↓
processURL(resolved)
  ↓
same-domain filter if enabled
  ↓
robotsManager.canFetch if robots enabled
  ↓
isHtmlPageUrl(processed.normalized)
  ↓
addURL(processed.normalized, depth + 1, current_url)
```

Current limitation:

- The final `isHtmlPageUrl()` check blocks some assets but is too simple.
- It can allow XML/sitemap as normal pages or block future non-HTML searchable resources incorrectly.
- Resource-type based filtering should replace `isHtmlPageUrl()`.

Recommended latest enqueue condition:

```cpp
ProcessedURL p = processURL(resolved_url);

if (p.status != URLStatus::ACCEPTED_URL) return;
if (p.crawlability != Crawlability::CRAWLABLE) return;
if (!shouldFetchBody(p.resource_type)) return;
if (!domain_allowed) return;
if (!ignore_robots && !robotsManager.canFetch(p.normalized)) return;

addURL(p.normalized, depth + 1, current_url);
```

Do not reject only because `p.priority == 0`.

---

## 13. Media Search Design

For Images/Videos/Audio tabs, do not download media bodies in the main crawler.

Correct approach:

```text
fetch HTML
  ↓
extract media metadata from raw HTML before discarding raw HTML
  ↓
store media URL + source page + alt/title/surrounding text
  ↓
frontend renders media search results using metadata
```

Recommended future records:

```cpp
struct ImageRecord {
    uint64_t image_id;
    uint64_t source_doc_id;
    string image_url;
    string source_page_url;
    string alt_text;
    string page_title;
    string surrounding_text;
    int width = 0;
    int height = 0;
    time_t discovered_at = 0;
};
```

Similar records can be added for videos and audio.

Frontend tabs can call:

```text
/search?q=cpp&type=web
/search?q=cpp&type=images
/search?q=cpp&type=videos
/search?q=cpp&type=audio
/search?q=cpp&type=documents
```

Current API does not yet implement `type=` tabs.

---

## 14. RocksDB Storage Model

Column families from `db_schema.h`:

| Column family | Key | Value | Purpose |
|---|---|---|---|
| `default` | internal keys | strings/JSON | pending URLs, next doc id, recrawl state, clicks |
| `doc_core_cf` | `url_hash` | `DocCore` JSON | identity and URL metadata |
| `domain_index_cf` | `d:{reversed_host}|/|{doc_id}` | `url_hash` | domain-ordered lookup |
| `fetch_meta_cf` | `url_hash` | `FetchMeta` JSON | fetch metadata |
| `content_meta_cf` | `url_hash` | `ContentMeta` JSON | content hash/simhash/duplicate fields |
| `parsed_content_cf` | `url_hash` | `ParsedContent` JSON | title, description, clean text |
| `link_graph_cf` | `url_hash` | `LinkData` JSON | link counts and PageRank placeholder |
| `quality_cf` | `url_hash` | `QualitySignals` JSON | quality/spam/readability/freshness fields |
| `presentation_cf` | `url_hash` | `Presentation` JSON | snippet/favicon/display URL |
| `control_cf` | `url_hash` | `ControlFlags` JSON | robots/noindex/nofollow/index status |

Internal default keys:

| Key | Purpose |
|---|---|
| `next_doc_id` | next numeric document id |
| `__pending_urls__` | JSON array of pending URLs for resume |
| `recrawl:{url_hash}` | recrawl scheduling state |
| `click:{doc_id}` | click count for API ranking/click tracking |

Primary join key:

```text
url_hash = sha256(normalized_url)
```

Current limitation:

- Canonical URL exists in `DocCore`, but `url_hash` is still based on normalized URL, not canonical URL.
- Site-specific canonicalization like RFC Editor variants should be added before dedup/indexing.

---

## 15. Stored Record Schemas

### 15.1 DocCore

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

Use:

- identity
- clickable URL
- language filter
- join key across CFs
- domain index lookup

### 15.2 FetchMeta

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

Use:

- skip non-200 documents
- freshness ranking
- crawl depth penalty
- content length filtering

### 15.3 ContentMeta

```json
{
  "content_hash": "sha256(clean_text)",
  "simhash": 123456789,
  "is_duplicate": false,
  "canonical_doc_id": 0
}
```

Use:

- exact duplicate detection
- near duplicate detection
- duplicate suppression in search

### 15.4 ParsedContent

```json
{
  "title": "Example Title",
  "meta_description": "Short description",
  "clean_text": "Visible text...",
  "token_count": 350
}
```

This is the main search input.

Recommended search weights:

```text
title: 4x or higher
meta_description: 2x
clean_text: 1x
URL: small boost
```

### 15.5 LinkData

```json
{
  "outbound_links_count": 42,
  "inbound_links_count": 0,
  "pagerank_score": 0.0
}
```

Current limitation:

- Actual edges are not stored.
- Inbound count and PageRank are placeholders unless computed later.

### 15.6 QualitySignals

```json
{
  "content_last_changed_time": 1710000000,
  "update_frequency": 1.0,
  "spam_score": 0.1,
  "quality_score": 0.81,
  "readability_score": 0.9
}
```

Use as secondary ranking features, not primary relevance.

### 15.7 Presentation

```json
{
  "snippet": "Beginning of clean page text...",
  "favicon_url": "/favicon.ico",
  "site_name": "",
  "breadcrumb": "",
  "display_url": "https://example.com/page"
}
```

Current API can generate query-aware snippets using `SnippetGenerator`, so static snippet is a fallback/presentation source.

### 15.8 ControlFlags

```json
{
  "robots_allowed": true,
  "noindex": false,
  "nofollow": false,
  "index_status": "PENDING",
  "error_reason": ""
}
```

Search API excludes `noindex == true`.

---

## 16. Recrawl System

`RocksDBStore::recordCrawlResult` writes `recrawl:{url_hash}` state into the default CF.

Recrawl state shape:

```json
{
  "normalized_url": "https://example.com/page",
  "last_crawl_ts": 1710000000,
  "next_crawl_ts": 1710600000,
  "failure_count": 0,
  "change_rate": 0.0,
  "last_content_hash": "...",
  "last_http_status": 200,
  "priority_score": 50
}
```

Status handling:

```text
retryable status: 0, 408, 429, >=500
non-retryable: normal success and most 4xx
```

Failure backoff:

```text
1 hour << failure_count, capped between 1 hour and 14 days
```

Successful recrawl interval:

```text
base around 1 week
shorter for high change_rate
shorter for higher priority_score
clamped between 1 hour and 1 week
```

`--crawl-database` behavior:

1. Call `getUrlsBatch(10000)` to select due recrawl candidates.
2. If none are due, fallback to `getAllDocumentUrls(10000)`.
3. Use selected URLs as seeds.

---

## 17. Duplicate Cleanup

`--remove-duplicates` calls `RocksDBStore::removeDuplicateURLs()`.

It can remove:

```text
duplicate doc_core records
duplicate domain_index entries
duplicate pending URLs
```

It can also repair `next_doc_id`.

Current duplicate cleanup is URL-record oriented. It is not full content duplicate clustering.

---

## 18. Search Index Loading

`QueryEngine::load(store)` calls `IndexSearcher::load(store)`.

`IndexSearcher` loads documents via:

```cpp
store.forEachSearchDocument(...)
```

It filters documents:

```text
skip if doc_id == 0
skip if normalized_url empty
skip if fetch_status_code != 200
skip if clean_text empty
skip if control_flags.noindex == true
skip if content_meta.is_duplicate == true
skip if quality_signals.spam_score > 0.8
```

Then it tokenizes separately:

```text
body: parsed_content.clean_text
title: parsed_content.title
description: parsed_content.meta_description
url: doc_core.normalized_url
```

Posting fields:

```cpp
struct Posting {
    uint64_t doc_id;
    uint32_t term_frequency;
    uint32_t title_frequency;
    uint32_t description_frequency;
    uint32_t url_frequency;
};
```

Index structures:

```text
term_to_id_ : token -> term id
id_to_term_ : term id -> token
inverted_index_ : term id -> postings
documents_ : doc id -> IndexedDocument
```

---

## 19. Query Processing

`QueryProcessor` does:

- lowercase ASCII text
- keeps alphanumeric plus meaningful symbols `+` and `#`
- strips apostrophes and curly apostrophes
- collapses punctuation/whitespace
- tokenizes by spaces
- removes stopwords
- stems simple English suffixes: `ing`, `ies`, `es`, `s`
- expands synonyms from `VairagyaEngine/config/synonyms.json`

Stopwords include words like:

```text
a, an, and, are, as, at, be, best, by, for, from, how, in, is, it, of, on, or, that, the, this, to, was, what, when, where, which, with
```

Fuzzy fallback:

- If retrieval returns no candidates, `QueryEngine` tries fuzzy expansion.
- It uses edit distance up to 2.
- It only considers vocabulary terms with the same first character.
- It adds up to 3 fuzzy matches per missing token.

---

## 20. Ranking

Ranking uses BM25 plus field boosts and metadata boosts.

Important boosts:

```text
title term frequency: strong boost
description term frequency: medium boost
URL frequency: small boost
query coverage: small boost
exact title equals query: very strong boost
title starts with query: strong boost
title/body phrase hits: strong boost
URL contains query: boost
proximity in title/body: boost
freshness: small boost
authority multiplier: click_count + inbound_links + pagerank + quality_score
spam penalty
crawl depth penalty
thin document penalty
many outbound + zero inbound penalty
```

BM25 constants:

```cpp
k1 = 1.5
b = 0.75
```

Click tracking:

- API `/click?doc_id=...` updates in-memory click count.
- It also increments `click:{doc_id}` in RocksDB.
- Query cache is cleared after click update.

---

## 21. Snippet Generation

`SnippetGenerator`:

- Takes snippet source text.
- Removes obvious JS/CSS/code-like noise with regex.
- Normalizes text.
- Finds first query token hit.
- Returns text around the hit with configurable radius.
- Falls back to beginning of text if no hit.

`IndexSearcher::cappedSnippet()` chooses snippet source in this order:

```text
meta_description
clean_text
title
normalized_url
```

Snippet source is capped to 4096 characters.

---

## 22. Current Known Limitations

Crawler limitations:

- `rate_limit.h` and `rate_limit.cpp` are empty in the uploaded snapshot.
- `resolver.h` and `resolver.cpp` are empty.
- No active per-host request delay.
- No active 429 host pause queue.
- `Retry-After` header is not parsed.
- Redirects are terminal and not followed.
- Sitemap URLs can be fetched and indexed as XML text instead of parsed/enqueued unless you add `SITEMAP_XML` handling.
- `ResourceType` is not in the uploaded archive yet, but is part of the current intended design.
- `isHtmlPageUrl()` is a simple extension blocklist and should be replaced with `ResourceType` logic.
- robots `Sitemap:` lines are parsed but not automatically enqueued.
- robots crawl-delay is not handled.
- SSL verification is disabled.
- Fetcher does not store ETag/Last-Modified response headers.
- HTML parser is regex-based.
- Raw HTML is not stored permanently.
- Media metadata indexes do not exist yet.
- Pending resume saves only URLs, not full `FrontierItem` metadata.
- Host fairness is weak because host queues are selected by unordered map iteration.
- Link graph stores counts, not actual edges.

Search/API limitations:

- Index is built in-memory at API startup.
- No durable postings index yet.
- `/search` supports only web results; no image/video/audio/document type tabs yet.
- No domain/language/freshness filters exposed in API yet.
- Ranking uses simple signals; PageRank/inbound links are not fully populated.
- No distributed sharding/partitioning.

---

## 23. Recommended Immediate Fixes

Priority order:

### 1. ResourceType system

Add:

```cpp
enum class ResourceType {
    HTML_PAGE,
    TEXT_DOCUMENT,
    SITEMAP_XML,
    PDF_DOCUMENT,
    IMAGE,
    VIDEO,
    AUDIO,
    STATIC_ASSET,
    UNKNOWN
};
```

Update `ProcessedURL`, `processURL`, `classifyResourceType`, and link enqueue logic.

### 2. Sitemap handling

For `SITEMAP_XML`:

```text
fetch body
extract <loc> URLs
process/enqueue discovered URLs
do not index sitemap as normal page
```

### 3. 429 handling

Add:

```text
FetchResult.retry_after_ms
per-host pause state
paused_hosts_429 min-heap
exponential backoff
special CLIENT_ERROR handling for 429
```

### 4. Redirect handling

Add:

```text
FetchResult.redirect_url
resolve Location against current URL
max redirect count
canonical/alias storage
crawl final destination
```

### 5. Media metadata extraction

During HTML parsing:

```text
extract <img src alt>
extract og:image/twitter:image
extract video/audio/iframe metadata
store separate media records
```

### 6. API type filters

Add:

```text
/search?q=...&type=web
/search?q=...&type=images
/search?q=...&type=videos
/search?q=...&type=audio
/search?q=...&type=documents
```

---

## 24. Recommended Commands

For one domain, avoid high thread counts:

```bash
VairagyaEngine.exe --mode crawler -d https://cplusplus.com/ -db vengine_tech_main -sd -cl -t 1
```

For sitemap seed after sitemap handling is implemented:

```bash
VairagyaEngine.exe --mode crawler -d https://www.netacad.com/sitemap.xml -db vengine_tech_main -sd -cl -t 1
```

Resume:

```bash
VairagyaEngine.exe --mode crawler --resume-db -db vengine_tech_main -sd -cl -t 1
```

Start API:

```bash
VairagyaEngine.exe --mode api -db vengine_tech_main --port 8080
```

Query:

```bash
curl "http://127.0.0.1:8080/search?q=c%2B%2B%20vector&page=1&limit=10"
```

Health:

```bash
curl "http://127.0.0.1:8080/health"
```

Click:

```bash
curl "http://127.0.0.1:8080/click?doc_id=123"
```


## 25. Important Design Rules Going Forward

### Rule 1: Priority is ranking for crawl order, not validity

```text
priority 0 = lowest priority
not automatically invalid
```

Validity should depend on:

```text
URLStatus
Crawlability
ResourceType
robots decision
same-domain policy
```

### Rule 2: Do not download media bodies in main crawler

```text
Image/video/audio search should use metadata extracted from HTML.
```

### Rule 3: Do not index sitemaps as pages

```text
sitemap.xml is a URL discovery file, not a search result.
```

### Rule 4: Do not handle 429 by rotating DNS resolvers

```text
429 should pause the host and obey Retry-After/backoff.
```

### Rule 5: Do not move every URL on 429

```text
Keep per-host queue.
Pause host.
Wake host later.
```

### Rule 6: Canonicalize known duplicate URL families

Example RFC Editor:

```text
/rfc/rfc1264.html
/refs/ref1264.txt
/info/rfc1264/
/rfc/rfc1264.txt
```

should collapse to one canonical search document, probably:

```text
https://www.rfc-editor.org/rfc/rfc1264.txt
```

---

## 26. Current Best Mental Model

VairagyaEngine now has three layers:

```text
Crawler Layer
  fetches URLs, respects robots, parses content, stores records

Storage Layer
  RocksDB document metadata, content, recrawl state, pending URLs, click counts

Search/API Layer
  loads documents, builds in-memory inverted index, ranks results, serves API
```

The next major engineering work should be:

```text
1. Resource-type aware crawler: Implemented at URL intake level.
   Remaining: ensure every frontier insertion path uses processURL(), including extracted links, redirects, sitemap URLs, and resume URLs.
2. sitemap parser
3. 429 host backoff
4. redirect following
5. media metadata schema
6. API result-type tabs
7. persistent inverted index
```
