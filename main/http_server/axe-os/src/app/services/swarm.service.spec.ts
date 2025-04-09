import { TestBed } from '@angular/core/testing';
import { SwarmService } from './swarm.service';
import { HttpTestingController, provideHttpClientTesting } from '@angular/common/http/testing';
import { SystemInfo, SystemInfoASICModelEnum } from '../generated/model/system-info';
import { Settings } from '../generated';

describe('SwarmService', () => {
  let service: SwarmService;
  let httpMock: HttpTestingController;

  const testIp = '192.168.0.100';
  const baseUrl = `http://${testIp}`;

  beforeEach(() => {
    TestBed.configureTestingModule({
      providers: [SwarmService]
    });

    service = TestBed.inject(SwarmService); 
    httpMock = TestBed.inject(HttpTestingController);
  });

  afterEach(() => {
    httpMock.verify(); // ensure no unmatched requests
  });

  it('should be created', () => {
    expect(service).toBeTruthy();
  });

  it('should call getSystemInfo with the correct IP', () => {
    const mockResponse: SystemInfo = {
      ASICModel: SystemInfoASICModelEnum.BM1366,
      apEnabled: 1,
      asicCount: 1,
      autofanspeed: 1,
      bestDiff: '50T',
      bestSessionDiff: '40T',
      boardVersion: 'v1.0',
      coreVoltage: 850,
      coreVoltageActual: 840,
      current: 2800,
      fallbackStratumPort: 4444,
      fallbackStratumURL: 'stratum+tcp://backup.pool',
      fallbackStratumUser: 'backupWorker',
      fanrpm: 2800,
      fanspeed: 75,
      temptarget: 65,
      flipscreen: 0,
      freeHeap: 95000,
      frequency: 400,
      hashRate: 120,
      hostname: 'bitaxe-1',
      idfVersion: 'v5.0',
      invertfanpolarity: 0,
      invertscreen: 0,
      isPSRAMAvailable: 1,
      isUsingFallbackStratum: 0,
      macAddr: '00:11:22:33:44:55',
      maxPower: 60,
      nominalVoltage: 12,
      overheat_mode: 0,
      overclockEnabled: 1,
      power: 52,
      power_fault: undefined,
      runningPartition: 'ota_0',
      sharesAccepted: 150,
      sharesRejected: 5,
      sharesRejectedReasons: [{message: 'Stale', count: 1337}],
      smallCoreCount: 2,
      ssid: 'MyWiFi',
      stratumDiff: 1.5,
      stratumPort: 3333,
      stratumURL: 'stratum+tcp://pool.example.com',
      stratumUser: 'worker1',
      temp: 60,
      uptimeSeconds: 123456,
      version: '1.1.0',
      voltage: 11.9,
      vrTemp: 68,
      wifiStatus: 'connected',
      wifiRSSI: -55
    };    

    service.getSystemInfo(testIp).subscribe(info => {
      expect(info).toEqual(mockResponse);
    });

    const req = httpMock.expectOne(`${baseUrl}/api/system/info`);
    expect(req.request.method).toBe('GET');
    req.flush(mockResponse);
  });

  it('should call restartSystem with the correct IP', () => {
    service.restartSystem(testIp).subscribe(response => {
      expect(response).toBe('OK');
    });

    const req = httpMock.expectOne(`${baseUrl}/api/system/restart`);
    expect(req.request.method).toBe('POST');
    req.flush('OK');
  });

  it('should call updateSystemSettings with the correct IP and payload', () => {
    const settings: Settings = {
      stratumURL: 'stratum+tcp://pool.example.com',
      stratumPort: 3333,
      stratumUser: 'worker1'
    };

    service.updateSystemSettings(testIp, settings).subscribe(response => {
      expect(response).toBeUndefined();
    });

    const req = httpMock.expectOne(`${baseUrl}/api/system`);
    expect(req.request.method).toBe('PATCH');
    expect(req.request.body).toEqual(settings);
    req.flush(null);
  });
});
