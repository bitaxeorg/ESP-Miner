import { HttpClient } from '@angular/common/http';
import { Injectable } from '@angular/core';
import { Observable } from 'rxjs';
import { map } from 'rxjs/operators';


interface GithubReleaseAsset {
  name: string;
  browser_download_url: string;
  // e.g. "sha256:<hex>", populated by GitHub for release assets
  digest?: string | null;
}

export interface GithubRelease {
  id: number;
  tag_name: string;
  name: string;
  html_url: string;
  prerelease: boolean;
  assets: GithubReleaseAsset[];
}

@Injectable({
  providedIn: 'root'
})
export class GithubUpdateService {

  constructor(
    private httpClient: HttpClient
  ) { }


  public getReleases(): Observable<GithubRelease[]> {
    return this.httpClient.get<GithubRelease[]>(
      'https://api.github.com/repos/bitaxeorg/esp-miner/releases'
    ).pipe(
      map((releases: GithubRelease[]) => releases.filter((release: GithubRelease) => !release.prerelease))
    );
  }

  public getReleaseByTag(tag: string): Observable<GithubRelease> {
    return this.httpClient.get<GithubRelease>(
      `https://api.github.com/repos/bitaxeorg/esp-miner/releases/tags/${encodeURIComponent(tag)}`
    );
  }

}
