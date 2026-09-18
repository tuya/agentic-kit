#!/usr/bin/env node
import { readdirSync, readFileSync, realpathSync, statSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const defaultSiteDir = fileURLToPath(new URL('../', import.meta.url));
const imageExtension = /\.(?:avif|bmp|gif|ico|jpe?g|png|svg|tiff?|webp)(?:[?#].*)?$/i;

function documents(root, prefix = '') {
  return readdirSync(path.join(root, prefix), { withFileTypes: true }).flatMap((entry) => {
    const name = path.join(prefix, entry.name);
    if (entry.isDirectory()) return documents(root, name);
    return /\.(?:md|mdx)$/.test(name) ? [name] : [];
  }).sort();
}

function prose(text) {
  let fence;
  let frontmatter = false;
  return text.split(/\r?\n/).map((line, index) => {
    if (index === 0 && line === '---') {
      frontmatter = true;
      return '';
    }
    if (frontmatter) {
      if (/^(?:---|\.\.\.)\s*$/.test(line)) frontmatter = false;
      return '';
    }
    const marker = line.match(/^ {0,3}(`{3,}|~{3,})(.*)$/);
    if (fence) {
      if (marker && marker[1][0] === fence[0] && marker[1].length >= fence.length && !marker[2].trim()) {
        fence = undefined;
      }
      return '';
    }
    if (marker) {
      fence = marker[1];
      return '';
    }
    return line;
  }).join('\n').replace(/<!--[\s\S]*?-->/g, '');
}

function headingIds(text) {
  return [...text.matchAll(/^ {0,3}#{1,6}[ \t]+.*?\{#([^}\s]+)\}[ \t]*(?:#+[ \t]*)?$/gm)]
    .map((match) => match[1]).sort();
}

// A deliberately small scanner for literal Markdown/MDX assets, not a JS evaluator.
function imageReferences(text, errors, document) {
  const refs = [];
  const normalize = (label) => label.trim().replace(/\s+/g, ' ').toLowerCase();
  const definitions = new Map();
  for (const match of text.matchAll(/^ {0,3}\[([^\]]+)\]:[ \t]*(?:<([^>\n]+)>|(\S+))/gm)) {
    definitions.set(normalize(match[1]), match[2] ?? match[3]);
  }
  const images = /!\[((?:\\.|[^\]\\])*)\](?:\(\s*(?:<([^>\n]*)>|((?:\\.|[^\s()\\]|\([^()\n]*\))*))(?:\s+(?:"[^"]*"|'[^']*'|\([^)]*\)))?\s*\)|[ \t]*\[([^\]]*)\])?/g;
  for (const match of text.matchAll(images)) {
    if (match[2] !== undefined || match[3] !== undefined) {
      refs.push(match[2] ?? match[3]);
    } else {
      const label = match[4] || match[1];
      const target = definitions.get(normalize(label));
      if (target !== undefined) refs.push(target);
      else if (match[4] !== undefined) errors.push(`${document}: undefined image reference [${label}]`);
    }
  }
  for (const match of text.matchAll(/^\s*import\s+(?:[^;'"\n]*(?:\n[^;'"\n]*)*?\s+from\s+)?['"]([^'"]+)['"]/gm)) {
    if (imageExtension.test(match[1])) refs.push(match[1]);
  }
  for (const tag of text.matchAll(/<[A-Za-z][^>]*>/g)) {
    const src = tag[0].match(/\s+src\s*=\s*(?:\{\s*(['"])(.*?)\1\s*\}|(['"])(.*?)\3|([^\s"'=<>`{}]+))/s);
    if (src) refs.push(src[2] ?? src[4] ?? src[5]);
  }
  return [...new Set(refs)];
}

function inside(root, target) {
  const relative = path.relative(root, target);
  return relative !== '..' && !relative.startsWith(`..${path.sep}`) && !path.isAbsolute(relative);
}

function realPath(file) {
  try {
    return realpathSync(file);
  } catch {
    return undefined;
  }
}

/** Return diagnostics without logging or exiting; defaults are independent of cwd. */
export function checkI18nParity({
  siteDir = defaultSiteDir,
  sourceDir = path.join(siteDir, 'docs'),
  localizedDir = path.join(siteDir, 'i18n/en/docusaurus-plugin-content-docs/current'),
  staticDir = path.join(siteDir, 'static'),
} = {}) {
  const errors = [];
  let sourceFiles;
  let localizedFiles;
  try {
    sourceFiles = documents(sourceDir);
    localizedFiles = documents(localizedDir);
  } catch (error) {
    return { errors: [`Cannot read document tree: ${error.message}`], sourceCount: 0, localizedCount: 0 };
  }
  const sourceSet = new Set(sourceFiles);
  const localizedSet = new Set(localizedFiles);
  for (const file of sourceFiles) {
    if (!localizedSet.has(file)) errors.push(`Missing English document: ${file}`);
  }
  for (const file of localizedFiles) {
    if (!sourceSet.has(file)) errors.push(`Extra English document: ${file}`);
  }
  const allowedRoots = [localizedDir, staticDir].map((root) => path.resolve(root));
  const realRoots = allowedRoots.map(realPath).filter(Boolean);
  const realSource = realPath(sourceDir);
  for (const file of localizedFiles) {
    const documentPath = path.join(localizedDir, file);
    const text = prose(readFileSync(documentPath, 'utf8'));
    if (sourceSet.has(file)) {
      const expected = headingIds(prose(readFileSync(path.join(sourceDir, file), 'utf8')));
      const actual = headingIds(text);
      if (JSON.stringify(expected) !== JSON.stringify(actual)) {
        errors.push(`${file}: explicit heading ID mismatch; source=${JSON.stringify(expected)}, English=${JSON.stringify(actual)}`);
      }
    }
    for (const reference of imageReferences(text, errors, file)) {
      if (/^(?:https?:|\/\/|data:|blob:|#)/i.test(reference)) continue;
      let target;
      try {
        const clean = decodeURIComponent(reference.split(/[?#]/, 1)[0]).replace(/\\([\\()[\] ])/g, '$1');
        if (clean.startsWith('@site/')) target = path.resolve(siteDir, clean.slice(6));
        else if (clean.startsWith('/')) target = path.resolve(staticDir, `.${clean}`);
        else target = path.resolve(path.dirname(documentPath), clean);
      } catch {
        errors.push(`${file}: invalid image URL: ${reference}`);
        continue;
      }
      const resolved = realPath(target);
      if (!allowedRoots.some((root) => inside(root, target)) ||
          (resolved && (!realRoots.some((root) => inside(root, resolved)) || inside(realSource, resolved)))) {
        errors.push(`${file}: image outside English/static trees (Chinese assets are not allowed): ${reference}`);
      } else if (!resolved || !statSync(resolved).isFile()) {
        errors.push(`${file}: missing image: ${reference}`);
      }
    }
  }
  return { errors, sourceCount: sourceFiles.length, localizedCount: localizedFiles.length };
}

if (process.argv[1] && realPath(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const result = checkI18nParity();
    if (result.errors.length) {
      console.error(`i18n parity failed (${result.errors.length} errors):\n${result.errors.map((error) => `- ${error}`).join('\n')}`);
      process.exitCode = 1;
    } else {
      console.log(`i18n parity OK: ${result.sourceCount} source / ${result.localizedCount} English documents; images and explicit heading IDs checked.`);
    }
  } catch (error) {
    console.error(`i18n parity failed: ${error.message}`);
    process.exitCode = 1;
  }
}
