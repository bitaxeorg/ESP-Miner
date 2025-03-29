import { ComponentFixture, TestBed } from '@angular/core/testing';

import { InfluxdbComponent } from './influxdb.component';

describe('InfluxdbComponent', () => {
  let component: InfluxdbComponent;
  let fixture: ComponentFixture<InfluxdbComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      declarations: [InfluxdbComponent]
    });
    fixture = TestBed.createComponent(InfluxdbComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
