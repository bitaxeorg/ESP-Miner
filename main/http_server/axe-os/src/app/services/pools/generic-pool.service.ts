import { Injectable } from '@angular/core';
import { MiningPool } from './mining-pool.interface';

@Injectable({
  providedIn: 'root',
})
export class GenericPoolService implements MiningPool {
  canHandle(_: string): boolean {
    // This generic service matches any stratumURL
    return true;
  }

  getRejectionExplanation(_: string): string | null {
    return null;
  }

  getQuickLink(stratumURL: string, stratumUser: string): string {
    const address = stratumUser.split('.')[0];

    if (stratumURL.includes('ocean.xyz')) {
      return `https://ocean.xyz/stats/${address}`;
    } else if (stratumURL.includes('solo.d-central.tech')) {
      return `https://solo.d-central.tech/#/app/${address}`;
    } else if (stratumURL.includes('pool.noderunners.network')) {
      return `https://noderunners.network/en/pool/user/${address}`;
    } else if (stratumURL.includes('satoshiradio.nl')) {
      return `https://pool.satoshiradio.nl/user/${address}`;
    } else if (stratumURL.includes('solohash.co.uk')) {
      return `https://solohash.co.uk/user/${address}`;
    }

    // Fallback: append the address to the stratumURL
    const normalizedUrl = stratumURL.startsWith('http') ? stratumURL : `http://${stratumURL}`;
    return `${normalizedUrl.replace(/\/$/, '')}/${encodeURIComponent(address)}`;
  }
}

