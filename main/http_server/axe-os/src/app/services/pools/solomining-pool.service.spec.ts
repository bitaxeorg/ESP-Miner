import { TestBed } from '@angular/core/testing';
import { NerdminerPoolService } from './nerdminer-pool.service';
import { SolominingPoolService } from './solomining-pool.service';

describe('SolominingPoolService', () => {
  let service: SolominingPoolService;
  const stratumUser = 'bc1qnp980s5fpp8l94p5cvttmtdqy8rvrq74qly2yrfmzkdsntqzlc5qkc4rkq.bitaxe';
  const address = 'bc1qnp980s5fpp8l94p5cvttmtdqy8rvrq74qly2yrfmzkdsntqzlc5qkc4rkq';

  beforeEach(() => {
    TestBed.configureTestingModule({
      providers: [SolominingPoolService]
    });
    service = TestBed.inject(SolominingPoolService);
  });

  describe('canHandle', () => {
    it('should return true for pool.solomining.de', () => {
      expect(service.canHandle('pool.solomining.de')).toBeTrue();
    });

    it('should return false for unrelated URLs', () => {
      expect(service.canHandle('ocean.xyz')).toBeFalse();
      expect(service.canHandle('somepool.org')).toBeFalse();
    });
  });

  describe('getQuickLink', () => {
    it('should return the correct Solomining dashboard link', () => {
      const result = service.getQuickLink('pool.solomining.de', stratumUser);
      expect(result).toBe(`https://pool.solomining.de/#/app/${address}`);
    });

    it('should encode special characters in the stratumUser address', () => {
      const specialUser = 'my@worker.bitaxe';
      const result = service.getQuickLink('pool.solomining.de', specialUser);
      expect(result).toBe('https://pool.solomining.de/#/app/my%40worker');
    });
  });
});
