import { ComponentFixture, TestBed } from '@angular/core/testing';

import { AutotuneComponent } from './autotune.component';

describe('SettingsComponent', () => {
  let component: AutotuneComponent;
  let fixture: ComponentFixture<AutotuneComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      declarations: [AutotuneComponent]
    });
    fixture = TestBed.createComponent(AutotuneComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
