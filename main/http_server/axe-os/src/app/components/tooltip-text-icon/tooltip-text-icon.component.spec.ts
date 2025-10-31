import { ComponentFixture, TestBed } from '@angular/core/testing';

import { TooltipTextIconComponent } from './tooltip-text-icon.component';

describe('WifiIconComponent', () => {
  let component: TooltipTextIconComponent;
  let fixture: ComponentFixture<TooltipTextIconComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      declarations: [TooltipTextIconComponent]
    });
    fixture = TestBed.createComponent(TooltipTextIconComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
