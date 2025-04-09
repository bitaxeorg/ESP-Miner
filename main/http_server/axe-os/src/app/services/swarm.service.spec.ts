import { TestBed } from '@angular/core/testing';

import { SwarmService } from './swarm.service';

describe('SwarmService', () => {
  let service: SwarmService;

  beforeEach(() => {
    TestBed.configureTestingModule({});
    service = TestBed.inject(SwarmService);
  });

  it('should be created', () => {
    expect(service).toBeTruthy();
  });
});
