import { ComponentFixture, TestBed } from '@angular/core/testing';

import { NetworkEditComponent } from './network.edit.component';
import { provideHttpClient } from '@angular/common/http';
import { HttpTestingController, provideHttpClientTesting } from '@angular/common/http/testing';
import { of } from 'rxjs';
import { provideToastr } from 'ngx-toastr';
import { DialogService } from 'src/app/services/dialog.service';
import { LiveDataService } from 'src/app/services/live-data.service';

describe('NetworkEditComponent', () => {
  let component: NetworkEditComponent;
  let fixture: ComponentFixture<NetworkEditComponent>;
  let http: HttpTestingController;
  let openDialog: jasmine.Spy;

  beforeEach(() => {
    TestBed.configureTestingModule({
      imports: [NetworkEditComponent],
      providers: [
        provideHttpClient(),
        provideHttpClientTesting(),
        provideToastr(),
        DialogService,
        { provide: LiveDataService, useValue: { info$: of({ hostname: 'bitaxe', ssid: 'test', useNTP: false }) } }
      ]
    });
    http = TestBed.inject(HttpTestingController);
    openDialog = spyOn(TestBed.inject(DialogService), 'open').and.returnValue(of(''));
    fixture = TestBed.createComponent(NetworkEditComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  afterEach(() => {
    http.verify();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });

  for (const ssid of ['constructor', 'toString', '__proto__']) {
    it(`should offer the strongest access point for SSID "${ssid}"`, () => {
      component.scanWifi();
      expect(component.scanning).toBeTrue();
      const request = http.expectOne('/api/system/wifi/scan');
      expect(request.request.method).toBe('GET');
      request.flush({ networks: [
        { ssid, rssi: -70, authmode: 3 },
        { ssid: 'ordinary-network', rssi: -50, authmode: 3 },
        { ssid, rssi: -40, authmode: 3 },
        { ssid: 'weak-network', rssi: -81, authmode: 3 }
      ] });

      expect(openDialog).toHaveBeenCalledOnceWith('Select Wi-Fi Network', [
        { label: ssid, rssi: -40, value: ssid },
        { label: 'ordinary-network', rssi: -50, value: 'ordinary-network' }
      ]);
      expect(component.scanning).toBeFalse();
    });
  }
});
