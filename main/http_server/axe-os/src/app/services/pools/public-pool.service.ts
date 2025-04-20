import { Injectable } from '@angular/core';
import { MiningPool } from './mining-pool.interface';

@Injectable({
  providedIn: 'root'
})
export class PublicPoolService implements MiningPool {
  
  private readonly explanations: Record<string, string> = {
    'Subscription validation error': 'The miner’s subscription request was invalid or malformed.',
    'Configuration validation error': 'Invalid mining configuration or settings sent by the miner.',
    'Authorization validation error': 'The credentials provided by the miner were incorrect or not authorized.',
    'Suggest difficulty validation error': 'An invalid difficulty value was suggested by the miner.',
    'Mining Submit validation error': 'The submitted mining work was invalid or improperly formatted.',
    'Job not found': 'While this share was being submitted, the network had already found a new block, making this share invalid.',
    'Duplicate share': 'This share has already been submitted.',
    'Difficulty too low': 'The submitted share does not meet the minimum required difficulty.',
  };

  canHandle(url: string): boolean {
    return url.includes('public-pool.io');
  }
  
  getRejectionExplanation(reason: string): string | null {
    return this.explanations[reason] ?? null;
  }
  
  getQuickLink(_: string, stratumUser: string): string | undefined {
    const address = stratumUser.split('.')[0];
    return `https://web.public-pool.io/#/app/${address}`;
  }
}
