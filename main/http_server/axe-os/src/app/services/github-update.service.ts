import { HttpClient } from '@angular/common/http';
import { Injectable } from '@angular/core';
import { Observable } from 'rxjs';

@Injectable({
  providedIn: 'root'
})
export class GithubUpdateService {

  constructor(
    private httpClient: HttpClient
  ) { }


  public getReleases() {
    return this.httpClient.get(`https://api.github.com/repos/skot/esp-miner/releases`) as Observable<any[]>;
  }

}