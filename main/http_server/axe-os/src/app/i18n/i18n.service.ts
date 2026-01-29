import { DOCUMENT } from '@angular/common';
import { Inject, Injectable, isDevMode } from '@angular/core';
import { BehaviorSubject } from 'rxjs';

import en from '../../assets/i18n/en.json';
import de from '../../assets/i18n/de.json';
import es from '../../assets/i18n/es.json';

export type Locale = 'en' | 'de' | 'es';

type TranslationValue = string | { [key: string]: TranslationValue };
type TranslationTable = Record<string, TranslationValue>;

const DEFAULT_LOCALE: Locale = 'en';
const STORAGE_KEY = 'axeos.locale';

const TRANSLATIONS: Record<Locale, TranslationTable> = {
  en,
  de,
  es
};

@Injectable({ providedIn: 'root' })
export class I18nService {
  private localeSubject = new BehaviorSubject<Locale>(DEFAULT_LOCALE);
  locale$ = this.localeSubject.asObservable();

  private missingKeys = new Set<string>();

  constructor(@Inject(DOCUMENT) private document: Document) {
    const initial = this.detectInitialLocale();
    this.localeSubject.next(initial);
    this.setDocumentLang(initial);
  }

  get locale(): Locale {
    return this.localeSubject.value;
  }

  setLocale(locale: Locale) {
    if (!this.isLocaleSupported(locale)) {
      return;
    }

    if (locale === this.localeSubject.value) {
      return;
    }

    this.localeSubject.next(locale);
    if (this.isBrowser()) {
      window.localStorage.setItem(STORAGE_KEY, locale);
    }
    this.setDocumentLang(locale);
  }

  t(key: string, params?: Record<string, string | number>): string {
    const localeValue = this.resolveKey(TRANSLATIONS[this.locale], key);
    const fallbackValue = this.resolveKey(TRANSLATIONS.en, key);
    const template = localeValue ?? fallbackValue;

    if (!template) {
      if (isDevMode()) {
        this.logMissingKey(key);
      }
      return key;
    }

    return this.interpolate(template, params);
  }

  private detectInitialLocale(): Locale {
    const saved = this.readSavedLocale();
    if (saved) {
      return saved;
    }

    const browser = this.getBrowserLocale();
    if (browser) {
      return browser;
    }

    return DEFAULT_LOCALE;
  }

  private readSavedLocale(): Locale | null {
    if (!this.isBrowser()) {
      return null;
    }

    const value = window.localStorage.getItem(STORAGE_KEY);
    if (value && this.isLocaleSupported(value)) {
      return value;
    }

    return null;
  }

  private getBrowserLocale(): Locale | null {
    if (!this.isBrowser()) {
      return null;
    }

    const raw = navigator.language || '';
    const normalized = raw.split('-')[0].toLowerCase();
    if (this.isLocaleSupported(normalized)) {
      return normalized as Locale;
    }

    return null;
  }

  private isLocaleSupported(value: string): value is Locale {
    return value === 'en' || value === 'de' || value === 'es';
  }

  private resolveKey(table: TranslationTable, key: string): string | null {
    const parts = key.split('.');
    let current: TranslationValue | undefined = table;

    for (const part of parts) {
      if (!current || typeof current !== 'object' || !(part in current)) {
        return null;
      }
      current = (current as Record<string, TranslationValue>)[part];
    }

    if (typeof current !== 'string') {
      return null;
    }

    return current;
  }

  private interpolate(template: string, params?: Record<string, string | number>): string {
    if (!params) {
      return template;
    }

    return template.replace(/\{(\w+)\}/g, (_match, token) => {
      const value = params[token];
      return value === undefined || value === null ? `{${token}}` : String(value);
    });
  }

  private logMissingKey(key: string) {
    if (this.missingKeys.has(key)) {
      return;
    }
    this.missingKeys.add(key);
    // eslint-disable-next-line no-console
    console.warn(`[i18n] Missing translation key: ${key}`);
  }

  private setDocumentLang(locale: Locale) {
    if (!this.document?.documentElement) {
      return;
    }
    this.document.documentElement.lang = locale;
  }

  private isBrowser(): boolean {
    return typeof window !== 'undefined';
  }
}
