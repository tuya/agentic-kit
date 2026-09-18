# Documentation Site

The site uses Docusaurus 3 and Node.js 20 or newer. Simplified Chinese (`zh-Hans`)
remains at `/`; English (`en`) is published at `/en/`. Documentation routes retain
identical suffixes in both languages, for example `/docs/intro` and `/en/docs/intro`.

## Install and Preview

Run these commands from `docs-site/`. Use npm and the checked-in
`package-lock.json`; do not regenerate another lockfile for translation work.

```bash
npm ci
npm run start -- --locale zh-Hans
# Stop the first server before starting the other locale on the same port.
npm run start -- --locale en
```

The development server serves one locale at a time. Test language switching with
the combined production build instead:

```bash
npm run test:i18n
npm run build
npm run serve -- --port 3000
```

`npm run build` runs TypeScript and localization parity checks through `prebuild`,
then builds both locales into `build/`. To run the checks separately:

```bash
npm run typecheck
npm run check:i18n
```

The parity gate checks exact Markdown/MDX paths, explicit heading IDs, and literal
local image references (Markdown images, reference-style images, MDX imports,
and HTML/JSX `src`). English images must resolve within the English content tree
or intentionally shared `static/` assets, never back into Chinese `docs/images/`.
It is not a JavaScript evaluator: use literal image references rather than
computed paths so the gate can inspect them. It does not prove translation
accuracy or freshness; bilingual review remains required.

## Translation Layout

```text
docs/                                          Chinese source pages and images
src/                                           Chinese React source messages
i18n/en/code.json                              English React and theme messages
i18n/en/docusaurus-theme-classic/navbar.json    Navbar labels and links
i18n/en/docusaurus-theme-classic/footer.json    Footer labels and links
i18n/en/docusaurus-plugin-content-docs/
  current.json                                 Sidebar category/version labels
  current/                                     English pages, matching docs/ paths
    images/                                    English diagrams and illustrations
static/                                        Intentionally shared assets
```

### Add or Update a Page

- Update the Chinese source and English counterpart in the same PR. Preserve
  filenames, IDs, slugs, sidebar positions, API symbols, protocol values, and
  literal runtime examples. Explain intentional Chinese example output in English.
- Keep the same explicit `{#heading-id}` on corresponding headings. Existing IDs
  may contain Chinese: they preserve bookmarks and locale-switch hashes and are
  not untranslated visible prose. Do not rename them when rewording a heading.
  For new headings, assign a stable ID in both files. The source helper is
  `npm run write-heading-ids -- --locale zh-Hans`; review its diff before copying
  new IDs to English. `markdown.mdx1Compat.headingIds` must stay enabled while
  using this syntax with the site's v4 compatibility setting.
- Use the module `CONTEXT.md` glossaries for domain vocabulary. Report unrelated
  source inaccuracies separately rather than silently diverging in translation.
- Keep one `sidebars.ts`. Translate page front matter and category labels in
  `current.json`; do not create a second sidebar.
- Keep relative links to translated local pages. Verify English external Tuya
  resources; when no English resource is available, label the original link as
  Chinese rather than assuming `/cn/` can be replaced with `/en/`.

### Update React and Navigation Messages

Use literal `translate({id, message, description})` calls or `<Translate id="...">`
with stable semantic IDs and Chinese source messages. Keep component keys and
behavior independent of translated labels. Then regenerate the English catalogs:

```bash
npm run write-translations -- --locale en
```

Docusaurus retains existing translated messages and adds missing keys. Review the
Git diff, translate newly extracted Chinese values, and update existing English
values whenever the source meaning changes; regeneration does not do that for
you. Do not replace the catalogs wholesale with fresh Chinese output. Keep
translator descriptions, and review any removed keys against their consumers.

### Localize Images

Place localized text-bearing assets in the English `current/images/` directory
and use relative references with descriptive English alt text. Shared branding
can stay in `static/`. The initial edition uses 17 English SVG schematics, with
captions explicitly distinguishing instructional diagrams from real screenshots.
Their descriptive control labels have not been verified against a logged-in
English Tuya Platform or App session. SVG filenames retain the original stems
but replace `.png`/`.jpg`; Chinese assets remain unchanged.

When replacing a schematic with a current English screenshot, verify the same
logical action, update its reference and caption, and use disposable accounts.
Remove account names, project/product IDs, App schemas, Access IDs/secrets,
credentials, device names, and usable QR codes. Never fabricate a screenshot or
copy live identifiers from Chinese source images. Check all images on desktop and
mobile; dense SVGs may need enlargement to read their details.

## Release Review and Deployment

Before release, build both locales and verify:

- All 29 current documents exist in each locale; neither locale has missing images
  or broken links. The parity count grows when new bilingual pages are added.
- Landing-page product tabs, capability states, clipboard success/failure,
  navigation, TOC, pagination, and accessibility labels are localized.
- Both language controls preserve equivalent routes. Test a tutorial, a guide,
  all reference pages, and section hashes (including `agent-trigger` and
  `reference/iot-client`) in both directions with a query string.
- Desktop and narrow-mobile layouts have no clipped controls or page-wide
  overflow. Tables and code blocks may scroll within their containers.
- Generated HTML has correct `lang`, canonical, and reciprocal `hreflang` values
  (`zh-Hans`, `en`, `x-default`). Review remaining Chinese text as intentional
  literals, proper names, or explicitly labeled Chinese resources.
- `build/index.html`, `build/docs/intro/index.html`, `build/en/index.html`,
  `build/en/docs/intro/index.html`, and `build/en/404.html` exist.

GitLab Pages and the GitHub Pages workflow both run the normal npm build and
publish its entire root, including `build/en/`; no separate locale job is needed.
The GitHub workflow currently triggers on `main` or manual dispatch, while this
repository uses `master`; use manual dispatch unless that separate trigger issue
is addressed. GitLab Pages covers both branch names.

An English `404.html` is generated, but the host chooses the fallback for unknown
URLs. GitHub Pages cannot route unknown `/en/*` URLs to a separate localized 404;
it may serve the root Chinese 404. Configure locale-aware fallbacks at a CDN only
if the deployment supports them.

Add the English-docs release note under `CHANGELOG.md`'s Unreleased/Added heading
with its actual `(#PR)` number once a PR is assigned; do not invent a number.
