/* eslint-disable no-console */
const fs = require('fs');
const path = require('path');

const baseDir = path.resolve(__dirname, '../src/assets/i18n');
const locales = ['en', 'de', 'es'];

function readJson(filePath) {
  const raw = fs.readFileSync(filePath, 'utf8');
  return JSON.parse(raw);
}

function flatten(obj, prefix = '', issues = []) {
  const keys = [];
  Object.entries(obj).forEach(([key, value]) => {
    const next = prefix ? `${prefix}.${key}` : key;
    if (value && typeof value === 'object' && !Array.isArray(value)) {
      keys.push(...flatten(value, next, issues));
      return;
    }
    if (typeof value !== 'string') {
      issues.push(`Non-string value at ${next}`);
      return;
    }
    keys.push(next);
  });
  return keys;
}

const data = {};
const errors = [];
const warnings = [];

for (const locale of locales) {
  const filePath = path.join(baseDir, `${locale}.json`);
  try {
    data[locale] = readJson(filePath);
  } catch (err) {
    errors.push(`Failed to read ${filePath}: ${err.message}`);
  }
}

if (!errors.length) {
  const enIssues = [];
  const enKeys = new Set(flatten(data.en, '', enIssues));
  if (enIssues.length) {
    errors.push(`en.json issues:\n  - ${enIssues.join('\n  - ')}`);
  }

  for (const locale of locales) {
    if (locale === 'en') continue;
    const issues = [];
    const localeKeys = new Set(flatten(data[locale], '', issues));

    if (issues.length) {
      errors.push(`${locale}.json issues:\n  - ${issues.join('\n  - ')}`);
    }

    const missing = [...enKeys].filter((key) => !localeKeys.has(key));
    if (missing.length) {
      errors.push(`Missing keys in ${locale}:\n  - ${missing.sort().join('\n  - ')}`);
    }

    const extra = [...localeKeys].filter((key) => !enKeys.has(key));
    if (extra.length) {
      warnings.push(`Unused keys in ${locale} (not in en):\n  - ${extra.sort().join('\n  - ')}`);
    }
  }
}

if (errors.length) {
  console.error('i18n check failed:');
  errors.forEach((msg) => console.error(`\n${msg}`));
  process.exitCode = 1;
} else {
  console.log('i18n check passed.');
}

if (warnings.length) {
  console.warn('\nWarnings:');
  warnings.forEach((msg) => console.warn(`\n${msg}`));
}
