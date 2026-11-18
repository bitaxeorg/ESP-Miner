import { TestBed } from '@angular/core/testing';
import { ShareRejectionExplanationService } from './share-rejection-explanation.service';
import { I18nService } from 'src/app/i18n/i18n.service';
import en from '../../assets/i18n/en.json';

describe('ShareRejectionExplanationService', () => {
  let service: ShareRejectionExplanationService;
  let i18n: I18nService;

  beforeEach(() => {
    TestBed.configureTestingModule({});
    service = TestBed.inject(ShareRejectionExplanationService);
    i18n = TestBed.inject(I18nService);
    i18n.setLocale('en');
  });

  it('should return explanations for known keys from locale bundle', () => {
    for (const message of Object.keys(en.share_rejection)) {
      expect(service.getExplanation(message))
        .withContext(`Key: ${message}`)
        .toBe(i18n.t(`share_rejection.${message}.explanation`));
    }
  });

  it('should return null for unknown key', () => {
    expect(service.getExplanation('This key does not exist')).toBeNull();
  });

  it('should return labels for known keys and fall back to raw text', () => {
    expect(service.getLabel('Above target')).toBe('Above target');
    expect(service.getLabel('This key does not exist')).toBe('This key does not exist');
  });
});
