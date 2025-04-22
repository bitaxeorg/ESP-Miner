import { Injectable } from '@angular/core';
import { PublicPoolService } from './public-pool.service';

@Injectable({
  providedIn: 'root'
})
export class NerdminerPoolService extends PublicPoolService {
  override canHandle(url: string): boolean {
    return url.includes('pool.nerdminer.de');
  }
  
  override getQuickLink(_: string, stratumUser: string): string | undefined {
    const address = stratumUser.split('.')[0];
    return `https://pool.nerdminer.de/#/app/${address}`;
  }
}
