import { I18nService } from './i18n.service';
import { TranslatePipe } from './translate.pipe';

describe('TranslatePipe', () => {
  let service: I18nService;
  let pipe: TranslatePipe;

  beforeEach(() => {
    window.localStorage.removeItem('axeos.locale');
    service = new I18nService(document);
    pipe = new TranslatePipe(service);
  });

  it('returns an empty string for empty keys', () => {
    expect(pipe.transform('')).toBe('');
  });

  it('returns a translation for a key', () => {
    service.setLocale('en');
    expect(pipe.transform('actions.save')).toBe('Save');
  });

  it('interpolates params', () => {
    service.setLocale('en');
    const message = pipe.transform('messages.device_restarted_at', { uri: 'http://axeos' });
    expect(message).toContain('http://axeos');
  });
});
