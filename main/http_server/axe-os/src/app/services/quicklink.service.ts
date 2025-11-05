import { Injectable } from '@angular/core';

type Pool =
  | { search: string; url: string }
  | { regex: RegExp; url: string };

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
    const user = stratumUser.split('.')[0];

    const pools: Pool[] = [
      { search: 'public-pool.io', url: `https://web.public-pool.io/#/app/${user}` },
      { search: 'nerdminer.de', url: `https://pool.nerdminer.de/#/app/${user}` },
      { search: 'solomining.de', url: `https://pool.solomining.de/#/app/${user}` },
      { search: 'yourdevice.ch', url: `https://blitzpool.yourdevice.ch/#/app/${user}` },
      { search: 'ocean.xyz', url: `https://ocean.xyz/stats/${user}` },
      { search: 'pool.noderunners.network', url: `https://noderunners.network/en/pool/user/${user}` },
      { search: 'satoshiradio.nl', url: `https://pool.satoshiradio.nl/user/${user}` },
      { search: 'solohash.co.uk', url: `https://solohash.co.uk/user/${user}` },
      { search: 'solo.stratum.braiins.com', url: `https://solo.braiins.com/stats/${user}` },
      { search: 'parasite.wtf', url: `https://parasite.space/user/${user}` },
      { regex: /^(eu|au)?solo[46]?.ckpool\.org/, url: `https://$1solostats.ckpool.org/users/${user}` },
    ];

    for (const pool of pools) {
      if ('search' in pool && stratumURL.includes(pool.search)) {
        return pool.url;
      }
      if ('regex' in pool) {
        const match = pool.regex.exec(stratumURL);
        if (match) return pool.url.replace(/\$(\d+)/g, (_, group) => match[+group] ?? '');
      }
    }

    return undefined;
  }
}
