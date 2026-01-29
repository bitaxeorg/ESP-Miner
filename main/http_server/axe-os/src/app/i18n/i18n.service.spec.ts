import { DOCUMENT } from '@angular/common';
import { TestBed } from '@angular/core/testing';

import { I18nService } from './i18n.service';

describe('I18nService', () => {
  let service: I18nService;

  beforeEach(() => {
    window.localStorage.removeItem('axeos.locale');
    TestBed.configureTestingModule({
      providers: [
        I18nService,
        { provide: DOCUMENT, useValue: document }
      ]
    });
    service = TestBed.inject(I18nService);
  });

  it('returns a translation for a known key', () => {
    service.setLocale('en');
    expect(service.t('nav.dashboard')).toBe('Dashboard');
  });

  it('interpolates params', () => {
    service.setLocale('en');
    const message = service.t('messages.device_restarted_at', { uri: 'http://axeos' });
    expect(message).toContain('http://axeos');
  });

  it('returns the key when missing', () => {
    service.setLocale('en');
    expect(service.t('missing.key')).toBe('missing.key');
  });

  it('persists the locale selection', () => {
    const setItemSpy = spyOn(window.localStorage, 'setItem');
    service.setLocale('de');
    expect(setItemSpy).toHaveBeenCalledWith('axeos.locale', 'de');
  });

  it('updates the document language', () => {
    service.setLocale('es');
    expect(document.documentElement.lang).toBe('es');
  });
});
