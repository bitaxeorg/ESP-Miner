import { GenericPoolService } from './generic-pool.service';

describe('GenericPoolService', () => {
  let service: GenericPoolService;
  const stratumUser = 'bc1qnp980s5fpp8l94p5cvttmtdqy8rvrq74qly2yrfmzkdsntqzlc5qkc4rkq.bitaxe';

  beforeEach(() => {
    service = new GenericPoolService();
  });

  describe('canHandle', () => {
    it('should always return true', () => {
      expect(service.canHandle('anything')).toBeTrue();
      expect(service.canHandle('')).toBeTrue();
      expect(service.canHandle('random-pool.com')).toBeTrue();
    });
  });

  describe('getRejectionExplanation', () => {
    it('should always return null', () => {
      expect(service.getRejectionExplanation('any reason')).toBeNull();
      expect(service.getRejectionExplanation('')).toBeNull();
    });
  });

  describe('getQuickLink', () => {
    const address = 'bc1qnp980s5fpp8l94p5cvttmtdqy8rvrq74qly2yrfmzkdsntqzlc5qkc4rkq';
  
    it('should return ocean.xyz link if matched', () => {
      const link = service.getQuickLink('ocean.xyz', stratumUser);
      expect(link).toBe(`https://ocean.xyz/stats/${address}`);
    });
  
    it('should return d-central.tech link if matched', () => {
      const link = service.getQuickLink('solo.d-central.tech', stratumUser);
      expect(link).toBe(`https://solo.d-central.tech/#/app/${address}`);
    });
  
    it('should return noderunners.network link if matched', () => {
      const link = service.getQuickLink('pool.noderunners.network', stratumUser);
      expect(link).toBe(`https://noderunners.network/en/pool/user/${address}`);
    });
  
    it('should return satoshiradio.nl link if matched', () => {
      const link = service.getQuickLink('satoshiradio.nl', stratumUser);
      expect(link).toBe(`https://pool.satoshiradio.nl/user/${address}`);
    });
  
    it('should return solohash.co.uk link if matched', () => {
      const link = service.getQuickLink('solohash.co.uk', stratumUser);
      expect(link).toBe(`https://solohash.co.uk/user/${address}`);
    });
  
    it('should return fallback http link if stratumURL has no protocol and is unknown', () => {
      const link = service.getQuickLink('somepool.org', stratumUser);
      expect(link).toBe(`http://somepool.org/${address}`);
    });
  
    it('should return fallback https link if already formatted', () => {
      const link = service.getQuickLink('https://custompool.org', stratumUser);
      expect(link).toBe(`https://custompool.org/${address}`);
    });
  
    it('should trim trailing slashes before appending', () => {
      const link = service.getQuickLink('https://custompool.org/', stratumUser);
      expect(link).toBe(`https://custompool.org/${address}`);
    });
  
    it('should encode the address correctly for fallback URLs', () => {
      const specialUser = 'abc.def/ghi@xyz';
      const expected = 'http://unknownpool.org/abc';
      const result = service.getQuickLink('unknownpool.org', specialUser);
      expect(result).toBe(expected);
    });
  });
  
});
