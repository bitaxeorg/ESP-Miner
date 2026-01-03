import { ComponentFixture, TestBed } from '@angular/core/testing';

import { SoloChanceComponent } from './solo-chance.component';

describe('SoloChanceComponent', () => {
  let component: SoloChanceComponent;
  let fixture: ComponentFixture<SoloChanceComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      declarations: [SoloChanceComponent]
    });
    fixture = TestBed.createComponent(SoloChanceComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
