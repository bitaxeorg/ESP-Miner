import { ComponentFixture, TestBed } from '@angular/core/testing';

import { WifiIconComponent } from './wifi-icon.component';

describe('WifiIconComponent', () => {
  let component: WifiIconComponent;
  let fixture: ComponentFixture<WifiIconComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      declarations: [WifiIconComponent]
    });
    fixture = TestBed.createComponent(WifiIconComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
