import { TestBed } from '@angular/core/testing';

import { GithubUpdateService } from './github-update.service';

describe('GithubUpdateService', () => {
  let service: GithubUpdateService;

  beforeEach(() => {
    TestBed.configureTestingModule({});
    service = TestBed.inject(GithubUpdateService);
  });

  it('should be created', () => {
    expect(service).toBeTruthy();
  });
});
