import assert from 'node:assert/strict';
import { copyFileSync, mkdirSync, mkdtempSync, rmSync, statSync, symlinkSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { test } from 'node:test';
import { checkI18nParity } from './check-i18n-parity.mjs';

const english = 'i18n/en/docusaurus-plugin-content-docs/current';
const script = new URL('./check-i18n-parity.mjs', import.meta.url);

function fixture(t) {
  assert.ok(statSync('/tmp').isDirectory());
  const siteDir = mkdtempSync('/tmp/i18n-parity-');
  t.after(() => rmSync(siteDir, { recursive: true, force: true }));
  function put(name, text = '') {
    assert.ok(statSync(siteDir).isDirectory());
    mkdirSync(path.dirname(path.join(siteDir, name)), { recursive: true });
    writeFileSync(path.join(siteDir, name), text);
  }
  function pair(name, source = '# Source\n', translated = source) {
    put(`docs/${name}`, source);
    put(`${english}/${name}`, translated);
  }
  put('static/img/logo.svg', '<svg/>');
  pair('intro.md');
  return { siteDir, put, pair, check: () => checkI18nParity({ siteDir }) };
}

test('valid nested md/mdx pairs permit different image formats and translated headings', (t) => {
  const f = fixture(t);
  f.pair('guides/start.mdx', '## Source {#stable}\n![source](../images/a.png)',
    '## English {#stable}\n![English](../images/a.svg?raw#diagram)');
  f.put(`${english}/images/a.svg`, '<svg/>');
  f.put('docs/ignored.txt');
  assert.deepEqual(f.check(), { errors: [], sourceCount: 2, localizedCount: 2 });
});

test('reports exact missing and extra paths, including md versus mdx', (t) => {
  const f = fixture(t);
  f.put('docs/guides/topic.md');
  f.put(`${english}/guides/topic.mdx`);
  assert.deepEqual(f.check().errors, [
    'Missing English document: guides/topic.md',
    'Extra English document: guides/topic.mdx',
  ]);
});

test('missing document tree fails rather than silently passing', (t) => {
  const f = fixture(t);
  rmSync(path.join(f.siteDir, english), { recursive: true });
  assert.match(f.check().errors[0], /Cannot read document tree/);
});

test('missing images and directory targets fail, with no fallback to source assets', (t) => {
  const f = fixture(t);
  f.pair('intro.md', '# Source', '![missing](./images/a.png)\n![directory](./images)');
  f.put('docs/images/a.png');
  f.put(`${english}/images/other.png`);
  assert.equal(f.check().errors.length, 2);
  for (const error of f.check().errors) assert.match(error, /intro.md: missing image:/);
});

test('relative, alias, and encoded traversal cannot use Chinese assets', (t) => {
  const f = fixture(t);
  f.put('docs/images/a.svg');
  f.pair('intro.md', '', [
    '![relative](../../../../docs/images/a.svg)',
    '![alias](@site/docs/images/a.svg)',
    '![encoded](@site/static/%2e%2e/docs/images/a.svg)',
    '![sibling](../current-other/a.svg)',
  ].join('\n'));
  assert.equal(f.check().errors.length, 4);
  for (const error of f.check().errors) assert.match(error, /image outside English\/static trees/);
});

test('symlinked localized and shared assets cannot escape to Chinese assets', (t) => {
  const f = fixture(t);
  f.put('docs/a.svg');
  symlinkSync(path.join(f.siteDir, 'docs/a.svg'), path.join(f.siteDir, english, 'a.svg'));
  symlinkSync(path.join(f.siteDir, 'docs/a.svg'), path.join(f.siteDir, 'static/a.svg'));
  f.pair('intro.md', '', '![local](./a.svg)\n![shared](/a.svg)');
  assert.equal(f.check().errors.length, 2);
  for (const error of f.check().errors) assert.match(error, /image outside English\/static trees/);
});

test('MDX SVG imports are checked even when rendered as components', (t) => {
  const f = fixture(t);
  f.pair('architecture.mdx', '', "import Diagram from './images/architecture.svg';\n<Diagram />");
  assert.match(f.check().errors[0], /missing image: .\/images\/architecture.svg/);
  f.put(`${english}/images/architecture.svg`, '<svg/>');
  assert.deepEqual(f.check().errors, []);
  f.pair('architecture.mdx', '', "import Diagram from '@site/docs/architecture.svg';\n<Diagram />");
  assert.match(f.check().errors[0], /image outside English\/static trees/);
});

test('root-relative, alias, and relative static references are intentional shares', (t) => {
  const f = fixture(t);
  f.pair('intro.md', '', [
    '![logo](/img/logo.svg)',
    '![logo](@site/static/img/logo.svg)',
    '![logo](../../../../static/img/logo.svg)',
    "import Logo from '@site/static/img/logo.svg';",
    "import Link from '@docusaurus/Link';",
  ].join('\n'));
  assert.deepEqual(f.check().errors, []);
});

test('HTML/JSX literal src and reference-style images resolve locally', (t) => {
  const f = fixture(t);
  f.pair('intro.md', '', [
    '<img src="./a.svg" />',
    "<Image src={'./b.svg'} />",
    '<img src=./c.svg />',
    '![description][Full Label]',
    '![collapsed][]',
    '![shortcut]',
    '[full   label]: <./d.svg> "title"',
    '[collapsed]: ./e.svg',
    '[shortcut]: ./f.svg',
  ].join('\n'));
  assert.equal(f.check().errors.length, 6);
  for (const name of ['a', 'b', 'c', 'd', 'e', 'f']) f.put(`${english}/${name}.svg`);
  assert.deepEqual(f.check().errors, []);
});

test('undefined explicit reference-style images fail', (t) => {
  const f = fixture(t);
  f.pair('intro.md', '', '![image][missing]');
  assert.match(f.check().errors[0], /undefined image reference \[missing\]/);
});

test('Markdown image titles, spaces, and parentheses are supported', (t) => {
  const f = fixture(t);
  f.pair('intro.md', '', '![a](<./a b.svg> "title")\n![b](./a(b).svg)\n![c](./a%20b.svg)');
  f.put(`${english}/a b.svg`);
  f.put(`${english}/a(b).svg`);
  assert.deepEqual(f.check().errors, []);
});

test('remote, data, and fragment references are skipped', (t) => {
  const f = fixture(t);
  f.pair('intro.md', '', [
    '![remote](https://example.invalid/a.svg)',
    '![remote](http://example.invalid/a.svg)',
    '![remote](//example.invalid/a.svg)',
    '![inline](data:image/png;base64,abc)',
    '![fragment](#diagram)',
  ].join('\n'));
  assert.deepEqual(f.check().errors, []);
});

test('anchor drift, removed anchors, extra anchors, and duplicates fail', (t) => {
  const f = fixture(t);
  for (const translated of ['## English {#changed}', '## English', '## English {#stable}\n## Extra {#extra}',
    '## English {#stable}\n## Duplicate {#stable}']) {
    f.pair('intro.md', '## Source {#stable}', translated);
    assert.match(f.check().errors[0], /explicit heading ID mismatch/);
  }
});

test('fenced code, frontmatter, comments and non-heading IDs do not affect parity', (t) => {
  const f = fixture(t);
  f.pair('intro.md', '## Source {#stable}', [
    '---', 'title: Example {#metadata}', '---',
    '## English {#stable}', 'Ordinary text {#not-a-heading}',
    '````md', '## Code {#ignored}', '```', '![code](missing.svg)', '````',
    '~~~md', '## Code {#also-ignored}', '<img src="missing.svg" />', '~~~',
    '<!-- ## Comment {#comment}\n![comment](missing.svg) -->',
  ].join('\n'));
  assert.deepEqual(f.check().errors, []);
});

test('CLI defaults are cwd-independent and imports have no output or side effects', (t) => {
  const f = fixture(t);
  assert.ok(statSync(f.siteDir).isDirectory());
  mkdirSync(path.join(f.siteDir, 'scripts'));
  const copiedScript = path.join(f.siteDir, 'scripts/check-i18n-parity.mjs');
  copyFileSync(script, copiedScript);
  const run = () => spawnSync(process.execPath, [copiedScript], { cwd: '/tmp', encoding: 'utf8' });
  const valid = run();
  assert.equal(valid.status, 0, valid.stderr);
  assert.match(valid.stdout, /i18n parity OK: 1 source \/ 1 English/);
  f.put('docs/missing.md');
  const invalid = run();
  assert.equal(invalid.status, 1);
  assert.match(invalid.stderr, /Missing English document: missing.md/);
  const imported = spawnSync(process.execPath, ['--input-type=module', '-e', `await import(${JSON.stringify(copiedScript)})`],
    { cwd: '/tmp', encoding: 'utf8' });
  assert.equal(imported.status, 0, imported.stderr);
  assert.equal(imported.stdout + imported.stderr, '');
});
