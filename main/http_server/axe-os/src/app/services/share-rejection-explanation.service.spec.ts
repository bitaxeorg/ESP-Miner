import { TestBed } from '@angular/core/testing';
import { ShareRejectionExplanationService } from './share-rejection-explanation.service';
import shareRejectionExplanations from '../../assets/share-rejection-explanations.json';
import { I18nService } from 'src/app/i18n/i18n.service';

describe('ShareRejectionExplanationService', () => {
  let service: ShareRejectionExplanationService;
  let i18n: I18nService;

  beforeEach(() => {
    TestBed.configureTestingModule({});
    service = TestBed.inject(ShareRejectionExplanationService);
    i18n = TestBed.inject(I18nService);
    i18n.setLocale('en');
  });

  it('should return explanations for known keys from JSON', () => {
    const merged = Object.assign({}, ...(shareRejectionExplanations as Array<Record<string, string | undefined>>));

    // Filter out undefineds during Map creation
    const filteredEntries = Object.entries(merged).filter(
      ([, value]) => typeof value === 'string'
    ) as [string, string][];
    
    const explanationMap = new Map<string, string>(filteredEntries);
    for (const [key, translationKey] of explanationMap) {
      expect(service.getExplanation(key)).withContext(`Key: ${key}`).toBe(i18n.t(translationKey));
    }
  });

  it('should return null for unknown key', () => {
    expect(service.getExplanation('This key does not exist')).toBeNull();
  });

  it('should return translated labels for known keys and fall back to raw text', () => {
    expect(service.getLabel('Above target')).toBe(i18n.t('miner.rejection_reasons.above_target'));
    expect(service.getLabel('This key does not exist')).toBe('This key does not exist');
  });
});
