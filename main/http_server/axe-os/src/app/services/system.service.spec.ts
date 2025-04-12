import { TestBed } from '@angular/core/testing';
import { SystemService } from './system.service';
import { HttpClientTestingModule } from '@angular/common/http/testing';

describe('SystemService', () => {
  let service: SystemService;

  beforeEach(() => {
    TestBed.configureTestingModule({
      imports: [HttpClientTestingModule],
      providers: [SystemService]
    });
    service = TestBed.inject(SystemService);
  });

  it('should be created', () => {
    expect(service).toBeTruthy();
  });
});
