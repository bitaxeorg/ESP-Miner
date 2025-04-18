import { Injectable } from '@angular/core';
import { PublicPoolService } from './public-pool.service';

@Injectable({
  providedIn: 'root'
})
export class SolominingPoolService extends PublicPoolService {
  override canHandle(url: string): boolean {
    return url.includes('pool.solomining.de');
  }
  
  override getQuickLink(_: string, stratumUser: string): string | undefined {
    const address = stratumUser.split('.')[0];
    return `https://pool.solomining.de/#/app/${address}`;
  }
}
