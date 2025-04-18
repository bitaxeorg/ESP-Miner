import { Injectable } from '@angular/core';

@Injectable({
  providedIn: 'root'
})
export class QuicklinkService {

  constructor() { }

  /**
   * Generates a quick link to a mining pool's user stats page based on the stratum URL and user
   * @param stratumURL The stratum server URL
   * @param stratumUser The stratum username (usually contains the wallet address)
   * @returns A URL to the pool's user stats page, or undefined if no matching pool is found
   */
  public getQuickLink(stratumURL: string, stratumUser: string): string | undefined {
    const address = stratumUser.split('.')[0];

    if (stratumURL.includes('public-pool.io')) {
      return `https://web.public-pool.io/#/app/${address}`;
    } else if (stratumURL.includes('ocean.xyz')) {
      return `https://ocean.xyz/stats/${address}`;
    } else if (stratumURL.includes('solo.d-central.tech')) {
      return `https://solo.d-central.tech/#/app/${address}`;
    } else if (/^eusolo[46]?.ckpool.org/.test(stratumURL)) {
      return `https://eusolostats.ckpool.org/users/${address}`;
    } else if (/^solo[46]?.ckpool.org/.test(stratumURL)) {
      return `https://solostats.ckpool.org/users/${address}`;
    } else if (stratumURL.includes('pool.noderunners.network')) {
      return `https://noderunners.network/en/pool/user/${address}`;
    } else if (stratumURL.includes('satoshiradio.nl')) {
      return `https://pool.satoshiradio.nl/user/${address}`;
    } else if (stratumURL.includes('solohash.co.uk')) {
      return `https://solohash.co.uk/user/${address}`;
    } else if (stratumURL.includes('nerdminer.de')) {
      return `https://pool.nerdminer.de/user/${address}`;
    } else if (stratumURL.includes('solomining.de')) {
      return `https://pool.solomining.de/user/${address}`;
    }
    return stratumURL.startsWith('http') ? stratumURL : `http://${stratumURL}`;
  }
}
