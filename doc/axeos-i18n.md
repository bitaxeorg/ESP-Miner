# AxeOS i18n Guide (de + es)

This document explains how internationalization (i18n) works in the AxeOS web UI and how to add or update translations.

## Scope
- Applies to the AxeOS web UI only (`main/http_server/axe-os`).
- Firmware and backend logic are out of scope.

## Where translations live
- Source files: `main/http_server/axe-os/src/assets/i18n/`
  - `en.json` (baseline)
  - `de.json`
  - `es.json`
- Share rejection mapping: `main/http_server/axe-os/src/assets/share-rejection-explanations.json`
  - Maps backend rejection reason strings to i18n keys under `miner.rejection_explanations.*` (tooltip explanations).
  - Labels use the same map with `miner.rejection_reasons.*` keys; if missing, the raw reason is shown.

## Key naming convention
Use dot-separated namespaces with stable identifiers:
- `nav.*`
- `actions.*`
- `common.*`
- `messages.*`
- `errors.*`
- `settings.*`
- `miner.*`
- `device.*`
- `units.*`

Keys should be stable identifiers, not full sentences.

## Glossary (authoritative)
Use consistent terminology from:
- `doc/axeos-i18n-glossary.md`

Global rules:
- Keep `Stale` unchanged in all languages.
- Keep `Shares` unchanged in all languages.
- Keep units unchanged: `°C`, `W`, `V`, `MHz`, `GH/s`, `TH/s`.

## How to use translations

### Templates (HTML)
Use the translate pipe:

```html
{{ 'nav.dashboard' | t }}
{{ 'messages.device_restarted_at' | t:{ uri: deviceUri } }}
```

### TypeScript
Inject `I18nService` and call `t()`:

```ts
this.toastr.success(this.i18n.t('messages.settings_saved'));
```

### Placeholders
Use `{token}` placeholders in JSON and pass a params object:

```json
"device_restarted_at": "Device at {uri} restarted"
```

```ts
this.i18n.t('messages.device_restarted_at', { uri: deviceUri })
```

## Locale selection
- Default locale: English (`en`).
- User selection is stored in `localStorage` under `axeos.locale`.
- If no saved preference exists, the browser language is used.
- Language selector is in Settings > UI.
- Language changes apply only after clicking Save.

## Add or update translations
1) Add or update the English key in `en.json`.
2) Add translations in `de.json` and `es.json`.
   - German uses informal Du; Spanish uses informal tu.
   - Keep labels short for tight UI spacing.
3) Replace hardcoded strings with `t()` or the `| t` pipe.
4) Run the i18n check (see below).

## Unit tests (UI)
- `main/http_server/axe-os/src/app/i18n/i18n.service.spec.ts`
- `main/http_server/axe-os/src/app/i18n/translate.pipe.spec.ts`

## i18n validation
Run from `main/http_server/axe-os`:

```bash
npm run i18n:check
```

The check validates:
- JSON validity.
- Missing keys in `de/es` compared to `en` (error).
- Extra keys in `de/es` not in `en` (warning).

## Build the web UI
From `main/http_server/axe-os`:

```bash
npm run build
```

If you want to skip API regeneration during development:

```bash
npm run build:no-api-gen
```

## Notes
- Keep strings short and UI-friendly (German expands).
- Avoid translating technical terms that are commonly used as-is in mining UIs.
