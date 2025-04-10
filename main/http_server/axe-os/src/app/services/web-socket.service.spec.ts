import { TestBed } from '@angular/core/testing';

import { WebsocketService } from './web-socket.service';

describe('WebSocketService', () => {
  let service: WebsocketService;

  beforeEach(() => {
    TestBed.configureTestingModule({});
    service = TestBed.inject(WebsocketService);
  });

  it('should be created', () => {
    expect(service).toBeTruthy();
  });
});
