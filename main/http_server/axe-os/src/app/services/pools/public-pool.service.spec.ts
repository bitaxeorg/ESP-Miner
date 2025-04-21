import { TestBed } from '@angular/core/testing';
import { PublicPoolService } from './public-pool.service';

describe('PublicPoolService', () => {
  let service: PublicPoolService;
  const stratumUser = 'bc1qnp980s5fpp8l94p5cvttmtdqy8rvrq74qly2yrfmzkdsntqzlc5qkc4rkq.bitaxe';
  const address = 'bc1qnp980s5fpp8l94p5cvttmtdqy8rvrq74qly2yrfmzkdsntqzlc5qkc4rkq';

  beforeEach(() => {
    TestBed.configureTestingModule({
      providers: [PublicPoolService]
    });
    service = TestBed.inject(PublicPoolService);
  });

  describe('canHandle', () => {
    it('should return true for public-pool.io domain', () => {
      expect(service.canHandle('public-pool.io')).toBeTrue();
    });

    it('should return false for other domains', () => {
      expect(service.canHandle('otherpool.org')).toBeFalse();
      expect(service.canHandle('ckpool.org')).toBeFalse();
    });
  });

  describe('getRejectionExplanation', () => {
    it('should return explanation for known rejection reasons', () => {
      expect(service.getRejectionExplanation('Subscription validation error')).toBe(
        'The miner’s subscription request was invalid or malformed.'
      );

      expect(service.getRejectionExplanation('Duplicate share')).toBe(
        'This share has already been submitted.'
      );
    });

    it('should return null for unknown rejection reasons', () => {
      expect(service.getRejectionExplanation('Unknown error')).toBeNull();
      expect(service.getRejectionExplanation('')).toBeNull();
    });
  });

  describe('getQuickLink', () => {
    it('should generate the correct link using the stratumUser address', () => {
      const link = service.getQuickLink('public-pool.io', stratumUser);
      expect(link).toBe(`https://web.public-pool.io/#/app/${address}`);
    });

    it('should encode special characters in the address', () => {
      const specialUser = 'my@worker.bitaxe';
      const result = service.getQuickLink('public-pool.io', specialUser);
      expect(result).toBe('https://web.public-pool.io/#/app/my%40worker');
    });
  });
});
