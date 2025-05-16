import { TestBed } from '@angular/core/testing';
import { ShareRejectionExplanationService } from './share-rejection-explanation.service';
import shareRejectionExplanations from '../../assets/share-rejection-explanations.json';

describe('ShareRejectionExplanationService', () => {
  let service: ShareRejectionExplanationService;

  beforeEach(() => {
    TestBed.configureTestingModule({});
    service = TestBed.inject(ShareRejectionExplanationService);
  });

  it('should return explanations for known keys from JSON', () => {
    const merged = Object.assign({}, ...(shareRejectionExplanations as Array<Record<string, string | undefined>>));

    // Filter out undefineds during Map creation
    const filteredEntries = Object.entries(merged).filter(
      ([, value]) => typeof value === 'string'
    ) as [string, string][];
    
    const explanationMap = new Map<string, string>(filteredEntries);    
    for (const [key, expectedValue] of explanationMap) {
      expect(service.getExplanation(key)).withContext(`Key: ${key}`).toBe(expectedValue);
    }
  });

  it('should return null for unknown key', () => {
    expect(service.getExplanation('This key does not exist')).toBeNull();
  });
});