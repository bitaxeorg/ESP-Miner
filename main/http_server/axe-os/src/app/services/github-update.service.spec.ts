import { TestBed } from '@angular/core/testing';
import { GithubUpdateService } from './github-update.service';
import { HttpClientTestingModule } from '@angular/common/http/testing';

describe('GithubUpdateService', () => {
  let service: GithubUpdateService;

  beforeEach(() => {
    TestBed.configureTestingModule({
      imports: [HttpClientTestingModule],
      providers: [GithubUpdateService]
    });
    service = TestBed.inject(GithubUpdateService);
  });

  it('should be created', () => {
    expect(service).toBeTruthy();
  });
});
