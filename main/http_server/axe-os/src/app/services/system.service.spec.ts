import { TestBed } from '@angular/core/testing';

import { SystemService } from './system.service';
import { provideHttpClient } from '@angular/common/http';

describe('SystemService', () => {
  let service: SystemService;

  beforeEach(() => {
    TestBed.configureTestingModule({
      providers: [provideHttpClient()]
    });
    service = TestBed.inject(SystemService);
  });

  it('should be created', () => {
    expect(service).toBeTruthy();
  });
});
