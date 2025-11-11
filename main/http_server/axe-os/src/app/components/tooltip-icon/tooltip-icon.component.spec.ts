import { ComponentFixture, TestBed } from '@angular/core/testing';

import { TooltipIconComponent } from './tooltip-icon.component';

describe('WifiIconComponent', () => {
  let component: TooltipIconComponent;
  let fixture: ComponentFixture<TooltipIconComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      declarations: [TooltipIconComponent]
    });
    fixture = TestBed.createComponent(TooltipIconComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
