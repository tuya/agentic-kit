import assert from 'node:assert/strict';
import {existsSync, readdirSync, readFileSync} from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import test from 'node:test';

const siteDir = fileURLToPath(new URL('../', import.meta.url));

for (const tree of ['docs', 'i18n/en/docusaurus-plugin-content-docs/current']) {
  test(`${tree}: relative document links include their source extension`, () => {
    const root = path.join(siteDir, tree);
    const errors = [];
    for (const file of readdirSync(root, {recursive: true})) {
      if (!/\.mdx?$/.test(file)) continue;
      const source = path.join(root, file);
      const text = readFileSync(source, 'utf8');
      // Check inline links and reference definitions used by these docs.
      const links = /\]\(<?([^\s)>]+)|^ {0,3}\[[^\]]+\]:\s*<?([^\s>]+)/gm;
      for (const match of text.matchAll(links)) {
        const target = match[1] ?? match[2];
        if (/^(?:[a-z][a-z\d+.-]*:|\/|#)/i.test(target)) continue;
        const pathname = target.split(/[?#]/, 1)[0];
        if (!pathname || path.extname(pathname)) continue;
        const resolved = path.resolve(path.dirname(source), pathname);
        if (['.md', '.mdx'].some(extension => existsSync(resolved + extension))) {
          errors.push(`${file}: ${target} must include .md or .mdx`);
        }
      }
    }
    assert.deepEqual(errors, [], errors.join('\n'));
  });
}
