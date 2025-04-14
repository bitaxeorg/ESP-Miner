import { TestBed } from '@angular/core/testing';
import { NerdminerPoolService } from './nerdminer-pool.service';

describe('NerdminerPoolService', () => {
  let service: NerdminerPoolService;
  const stratumUser = 'bc1qnp980s5fpp8l94p5cvttmtdqy8rvrq74qly2yrfmzkdsntqzlc5qkc4rkq.bitaxe';
  const address = 'bc1qnp980s5fpp8l94p5cvttmtdqy8rvrq74qly2yrfmzkdsntqzlc5qkc4rkq';

  beforeEach(() => {
    TestBed.configureTestingModule({
      providers: [NerdminerPoolService]
    });
    service = TestBed.inject(NerdminerPoolService);
  });

  describe('canHandle', () => {
    it('should return true for pool.nerdminer.de', () => {
      expect(service.canHandle('pool.nerdminer.de')).toBeTrue();
    });

    it('should return false for unrelated URLs', () => {
      expect(service.canHandle('ocean.xyz')).toBeFalse();
      expect(service.canHandle('somepool.org')).toBeFalse();
    });
  });

  describe('getQuickLink', () => {
    it('should return the correct Nerdminer dashboard link', () => {
      const result = service.getQuickLink('pool.nerdminer.de', stratumUser);
      expect(result).toBe(`https://pool.nerdminer.de/#/app/${address}`);
    });

    it('should encode special characters in the stratumUser address', () => {
      const specialUser = 'my@worker.bitaxe';
      const result = service.getQuickLink('pool.nerdminer.de', specialUser);
      expect(result).toBe('https://pool.nerdminer.de/#/app/my%40worker');
    });
  });
});
