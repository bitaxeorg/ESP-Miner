import { ComponentFixture, TestBed } from '@angular/core/testing';
import { FormsModule, ReactiveFormsModule } from '@angular/forms';
import { HttpClient, provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';
import { of } from 'rxjs';

import { SwarmComponent } from './swarm.component';
import { ModalComponent } from '../modal/modal.component';
import { TooltipTextIconComponent } from 'src/app/components/tooltip-text-icon/tooltip-text-icon.component';
import { DropdownComponent } from 'src/app/components/dropdown/dropdown.component';
import { SliderComponent } from 'src/app/components/slider/slider.component';
import { TooltipDirective } from 'src/app/directives/tooltip.directive';

import { HashSuffixPipe } from 'src/app/pipes/hash-suffix.pipe';
import { DiffSuffixPipe } from 'src/app/pipes/diff-suffix.pipe';
import { DateAgoPipe } from 'src/app/pipes/date-ago.pipe';
import { AddressPipe } from 'src/app/pipes/address.pipe';
import { SatsPipe } from 'src/app/pipes/sats.pipe';

describe('SwarmComponent', () => {
  let component: SwarmComponent;
  let fixture: ComponentFixture<SwarmComponent>;
  let httpClient: HttpClient;

  beforeEach(() => {
    TestBed.configureTestingModule({
      declarations: [
        SwarmComponent,
        ModalComponent,
        TooltipTextIconComponent
      ],
      imports: [
        ReactiveFormsModule,
        FormsModule,
        TooltipDirective,
        DropdownComponent,
        SliderComponent,
        HashSuffixPipe,
        DiffSuffixPipe,
        DateAgoPipe,
        AddressPipe,
        SatsPipe
      ],
      providers: [
        provideHttpClient(),
        provideToastr()
      ]
    });
    
    httpClient = TestBed.inject(HttpClient);
    spyOn(httpClient, 'get').and.callFake(((url: string) => {
      if (url.includes('/api/system/info')) {
        return of({ ipv4: '192.168.1.1', version: 'v2.1.2' });
      }
      return of({});
    }) as any);

    fixture = TestBed.createComponent(SwarmComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });

  it('should render swarm list details and custom components when devices are present', () => {
    component.swarm = [
      {
        address: 'bitaxe-1.local',
        displayName: 'Bitaxe 1',
        connectionAddress: '192.168.1.100',
        ASICModel: 'BM1366',
        deviceModel: 'Ultra',
        swarmColor: 'purple',
        asicCount: 1,
        hashRate: 500e9,
        sharesAccepted: 100,
        sharesRejected: 1,
        bestDiff: 1000,
        bestSessionDiff: 500,
        power: 15,
        temp: 55,
        version: 'v2.1.2',
        uptimeSeconds: 3600,
        poolDifficulty: 1000
      }
    ];
    fixture.detectChanges();

    const element = fixture.nativeElement;
    
    // Verify that components inside *ngIf are rendered
    expect(element.querySelector('app-slider')).toBeTruthy();
    expect(element.querySelector('app-modal')).toBeTruthy();
  });
});
