import { Injectable } from '@angular/core';
import { MiningPool } from './mining-pool.interface';
import { PublicPoolService } from './public-pool.service';
import { CkPoolService } from './ck-pool.service';
import { GenericPoolService } from './generic-pool.service';
import { SolominingPoolService } from './solomining-pool.service';

@Injectable({
  providedIn: 'root'
})
export class PoolFactoryService {
  private readonly pools: MiningPool[];

  constructor(
    publicPool: PublicPoolService,
    solominingPool: SolominingPoolService,
    nerdminerPool: PublicPoolService,
    ckPool: CkPoolService,
    private genericPool: GenericPoolService
  ) {
    this.pools = [publicPool, ckPool, solominingPool, nerdminerPool];
  }

  getPoolForUrl(url: string): MiningPool {
    const matched = this.pools.find(pool => pool.canHandle(url));
    return matched ?? this.genericPool;
  }
}
