import { TestBed } from '@angular/core/testing';
import { CkPoolService } from './ck-pool.service';

describe('CkPoolService', () => {
  let service: CkPoolService;

  beforeEach(() => {
    TestBed.configureTestingModule({});
    service = TestBed.inject(CkPoolService);
  });

  it('should be created', () => {
    expect(service).toBeTruthy();
  });

  describe('canHandle', () => {
    it('should return true for valid CKPool domains', () => {
      const validUrls = [
        'solo.ckpool.org',
        'solo4.ckpool.org',
        'solo6.ckpool.org',
        'eusolo.ckpool.org',
        'eusolo4.ckpool.org',
        'eusolo6.ckpool.org'
      ];

      validUrls.forEach(url => {
        expect(service.canHandle(url)).withContext(`URL: ${url}`).toBeTrue();
      });
    });

    it('should return false for unrelated domains', () => {
      const invalidUrls = [
        'pool.solomining.de',
        'public-pool.io',
        'example.com',
        'ckpool.org',
        'notckpool.org'
      ];

      invalidUrls.forEach(url => {
        expect(service.canHandle(url)).withContext(`URL: ${url}`).toBeFalse();
      });
    });
  });

  describe('getRejectionExplanation', () => {
    it('should return the correct explanation for known reasons', () => {
      expect(service.getRejectionExplanation('Above target')).toContain('does not meet the minimum required difficulty');
      expect(service.getRejectionExplanation('Stale')).toContain('the network had already found a new block');
    });

    it('should return null for unknown reasons', () => {
      expect(service.getRejectionExplanation('Unknown reason')).toBeNull();
      expect(service.getRejectionExplanation('')).toBeNull();
    });
  });

  describe('getQuickLink', () => {
    it('should return a valid quick link for CKPool URLs', () => {
      const user = '1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa';
      const url = 'eusolo6.ckpool.org';
      const expected = `https://eusolo6.ckpool.org/users/${encodeURIComponent(user)}`;

      expect(service.getQuickLink(url, user)).toEqual(expected);
    });

    it('should return undefined for non-CKPool URLs', () => {
      expect(service.getQuickLink('otherpool.org', 'someUser')).toBeUndefined();
    });
  });
});
