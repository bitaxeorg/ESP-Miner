import { TestBed } from '@angular/core/testing';
import { PoolFactoryService } from './pool-factory.service';
import { CkPoolService } from './ck-pool.service';
import { GenericPoolService } from './generic-pool.service';
import { PublicPoolService } from './public-pool.service';
import { SolominingPoolService } from './solomining-pool.service';
import { MiningPool } from './mining-pool.interface';
import { NerdminerPoolService } from './nerdminer-pool.service';

describe('PoolFactoryService', () => {
  let service: PoolFactoryService;

  let publicPool: jasmine.SpyObj<MiningPool>;
  let ckPool: jasmine.SpyObj<MiningPool>;
  let solominingPool: jasmine.SpyObj<MiningPool>;
  let nerdminerPool: jasmine.SpyObj<MiningPool>;
  let genericPool: jasmine.SpyObj<MiningPool>;

  beforeEach(() => {
    publicPool = jasmine.createSpyObj('PublicPoolService', ['canHandle', 'getQuickLink', 'getRejectionExplanation']);
    ckPool = jasmine.createSpyObj('CkPoolService', ['canHandle', 'getQuickLink', 'getRejectionExplanation']);
    solominingPool = jasmine.createSpyObj('SolominingPoolService', ['canHandle', 'getQuickLink', 'getRejectionExplanation']);
    nerdminerPool = jasmine.createSpyObj('NerdminerPoolService', ['canHandle', 'getQuickLink', 'getRejectionExplanation']);
    genericPool = jasmine.createSpyObj('GenericPoolService', ['canHandle', 'getQuickLink', 'getRejectionExplanation']);

    TestBed.configureTestingModule({
      providers: [
        PoolFactoryService,
        { provide: PublicPoolService, useValue: publicPool },
        { provide: CkPoolService, useValue: ckPool },
        { provide: SolominingPoolService, useValue: solominingPool },
        { provide: GenericPoolService, useValue: genericPool },
        { provide: NerdminerPoolService, useValue: nerdminerPool },
      ]
    });

    service = TestBed.inject(PoolFactoryService);
  });

  it('should return the pool whose canHandle returns true', () => {
    publicPool.canHandle.and.returnValue(false);
    ckPool.canHandle.and.returnValue(false);
    solominingPool.canHandle.and.returnValue(true); // only one matches
    nerdminerPool.canHandle.and.returnValue(false);
    genericPool.canHandle.and.returnValue(true); // shouldn't be returned

    const result = service.getPoolForUrl('pool.solomining.de');
    expect(result).toBe(solominingPool);
  });

  it('should return the first matching pool if multiple match', () => {
    publicPool.canHandle.and.returnValue(true); // matched first
    ckPool.canHandle.and.returnValue(true);
    solominingPool.canHandle.and.returnValue(true);
    nerdminerPool.canHandle.and.returnValue(false);

    const result = service.getPoolForUrl('someurl.com');
    expect(result).toBe(publicPool); // first match wins
  });

  it('should return generic pool if none match', () => {
    publicPool.canHandle.and.returnValue(false);
    ckPool.canHandle.and.returnValue(false);
    solominingPool.canHandle.and.returnValue(false);
    nerdminerPool.canHandle.and.returnValue(false);

    const result = service.getPoolForUrl('unknownpool.com');
    expect(result).toBe(genericPool);
  });
});
