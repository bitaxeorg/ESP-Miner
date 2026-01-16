import { TestBed } from '@angular/core/testing';

import { SystemApiService } from './system.service';
import { provideHttpClient } from '@angular/common/http';

describe('SystemApiService', () => {
  let service: SystemApiService;

  beforeEach(() => {
    TestBed.configureTestingModule({
      providers: [provideHttpClient()]
    });
    service = TestBed.inject(SystemApiService);
  });

  it('should be created', () => {
    expect(service).toBeTruthy();
  });
});
