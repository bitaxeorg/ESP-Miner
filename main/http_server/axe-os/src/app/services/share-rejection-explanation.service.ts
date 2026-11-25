import { Injectable } from '@angular/core';

import { I18nService } from 'src/app/i18n/i18n.service';
import en from '../../assets/i18n/en.json';

// Maps pool-provided rejection reason aliases (from en.json) to their canonical entry key,
// e.g. "Above Target" -> "difficulty-too-low".
const ALIASES: Map<string, string> = (() => {
  const map = new Map<string, string>();

  for (const [key, entry] of Object.entries(en.share_rejection)) {
    const aliases = (entry as { aliases?: unknown }).aliases;
    if (!Array.isArray(aliases)) {
      continue;
    }
    for (const alias of aliases) {
      if (typeof alias === 'string') {
        map.set(alias, key);
      }
    }
  }

  return map;
})();

@Injectable({
  providedIn: 'root'
})
export class ShareRejectionExplanationService {

  constructor(private i18n: I18nService) { }

  getLabel(key: string): string {
    const translated = this.translate(key, 'label');
    return translated ?? key;
  }

  getExplanation(key: string): string | null {
    return this.translate(key, 'explanation');
  }

  private translate(key: string, field: 'label' | 'explanation'): string | null {
    const translationKey = `share_rejection.${key}.${field}`;
    const translated = this.i18n.t(translationKey);
    if (translated !== translationKey) {
      return translated;
    }

    const canonical = ALIASES.get(key.toLowerCase());
    if (!canonical) {
      return null;
    }

    const aliasKey = `share_rejection.${canonical}.${field}`;
    const resolved = this.i18n.t(aliasKey);
    return resolved === aliasKey ? null : resolved;
  }
}
