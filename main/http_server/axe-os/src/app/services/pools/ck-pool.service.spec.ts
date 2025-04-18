import { TestBed } from '@angular/core/testing';
import { GenericPoolService } from './generic-pool.service';

describe('GenericPoolService', () => {
  let service: GenericPoolService;
  const stratumUser = 'bc1qnp980s5fpp8l94p5cvttmtdqy8rvrq74qly2yrfmzkdsntqzlc5qkc4rkq.bitaxe';
  const address = 'bc1qnp980s5fpp8l94p5cvttmtdqy8rvrq74qly2yrfmzkdsntqzlc5qkc4rkq';

  beforeEach(() => {
    TestBed.configureTestingModule({
      providers: [GenericPoolService],
    });
    service = TestBed.inject(GenericPoolService);
  });

  describe('canHandle', () => {
    it('should always return true', () => {
      expect(service.canHandle('any-url')).toBeTrue();
      expect(service.canHandle('')).toBeTrue();
    });
  });

  describe('getRejectionExplanation', () => {
    it('should always return null', () => {
      expect(service.getRejectionExplanation('any')).toBeNull();
    });
  });

  describe('getQuickLink', () => {
    it('should match ocean.xyz', () => {
      const url = service.getQuickLink('ocean.xyz', stratumUser);
      expect(url).toBe(`https://ocean.xyz/stats/${address}`);
    });

    it('should match solo.d-central.tech', () => {
      const url = service.getQuickLink('solo.d-central.tech', stratumUser);
      expect(url).toBe(`https://solo.d-central.tech/#/app/${address}`);
    });

    it('should match noderunners.network', () => {
      const url = service.getQuickLink('pool.noderunners.network', stratumUser);
      expect(url).toBe(`https://noderunners.network/en/pool/user/${address}`);
    });

    it('should match satoshiradio.nl', () => {
      const url = service.getQuickLink('satoshiradio.nl', stratumUser);
      expect(url).toBe(`https://pool.satoshiradio.nl/user/${address}`);
    });

    it('should match solohash.co.uk', () => {
      const url = service.getQuickLink('solohash.co.uk', stratumUser);
      expect(url).toBe(`https://solohash.co.uk/user/${address}`);
    });

    it('should fall back to http:// for unknown pools', () => {
      const url = service.getQuickLink('somepool.org', stratumUser);
      expect(url).toBe(`http://somepool.org/${address}`);
    });

    it('should preserve https:// if present', () => {
      const url = service.getQuickLink('https://example.com', stratumUser);
      expect(url).toBe(`https://example.com/${address}`);
    });

    it('should trim trailing slashes', () => {
      const url = service.getQuickLink('https://example.com/', stratumUser);
      expect(url).toBe(`https://example.com/${address}`);
    });

    it('should encode special characters in fallback address', () => {
      const result = service.getQuickLink('custompool.org', 'my@worker.name');
      expect(result).toBe('http://custompool.org/my%40worker');
    });
  });
});
