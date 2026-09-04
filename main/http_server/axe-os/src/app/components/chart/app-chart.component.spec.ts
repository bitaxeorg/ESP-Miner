import { ComponentFixture, TestBed } from '@angular/core/testing';
import { SimpleChange } from '@angular/core';
import { AppChartComponent } from './app-chart.component';

describe('AppChartComponent', () => {
  let component: AppChartComponent;
  let fixture: ComponentFixture<AppChartComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      imports: [AppChartComponent]
    });
    fixture = TestBed.createComponent(AppChartComponent);
    component = fixture.componentInstance;
    component.data = {
      labels: ['1', '2'],
      datasets: [{ data: [10, 20] }]
    };
    component.options = { responsive: true, animation: false };
    fixture.detectChanges();
    component.refresh();
  });

  afterEach(() => {
    component.ngOnDestroy();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });

  it('should initialize chart when visible', () => {
    expect(component.chart).toBeTruthy();
  });

  it('should not update chart when hidden and should mark pendingUpdate', () => {
    expect(component.chart).toBeTruthy();
    const updateSpy = spyOn(component.chart!, 'update');

    spyOnProperty(document, 'visibilityState', 'get').and.returnValue('hidden');

    component.data = {
      labels: ['1', '2', '3'],
      datasets: [{ data: [10, 20, 30] }]
    };
    component.ngOnChanges({
      data: new SimpleChange(null, component.data, false)
    });

    expect(updateSpy).not.toHaveBeenCalled();
    expect(component['pendingUpdate']).toBeTrue();
  });

  it('should update chart with "none" mode when visible', () => {
    expect(component.chart).toBeTruthy();
    const updateSpy = spyOn(component.chart!, 'update');

    spyOnProperty(document, 'visibilityState', 'get').and.returnValue('visible');

    component.data = {
      labels: ['1', '2', '3'],
      datasets: [{ data: [10, 20, 30] }]
    };
    component.ngOnChanges({
      data: new SimpleChange(null, component.data, false)
    });

    expect(updateSpy).toHaveBeenCalledWith('none');
    expect(component['pendingUpdate']).toBeFalse();
  });

  it('should execute pending update when visibility changes to visible', () => {
    expect(component.chart).toBeTruthy();
    const updateSpy = spyOn(component.chart!, 'update');
    const resizeSpy = spyOn(component.chart!, 'resize');
    spyOn(window, 'requestAnimationFrame').and.callFake((cb: FrameRequestCallback) => { cb(0); return 1; });

    // Simulate tab hidden
    let currentVisibility: DocumentVisibilityState = 'hidden';
    spyOnProperty(document, 'visibilityState', 'get').and.callFake(() => currentVisibility);

    component.ngOnChanges({
      data: new SimpleChange(null, component.data, false)
    });
    expect(updateSpy).not.toHaveBeenCalled();
    expect(component['pendingUpdate']).toBeTrue();

    // Tab becomes visible again
    currentVisibility = 'visible';
    component.onVisibilityChange();

    expect(resizeSpy).toHaveBeenCalled();
    expect(updateSpy).toHaveBeenCalledWith('none');
    expect(component['pendingUpdate']).toBeFalse();
  });

  it('should schedule resize when visible without pending update', () => {
    expect(component.chart).toBeTruthy();
    const resizeSpy = spyOn(component.chart!, 'resize');
    spyOn(window, 'requestAnimationFrame').and.callFake((cb: FrameRequestCallback) => { cb(0); return 1; });

    spyOnProperty(document, 'visibilityState', 'get').and.returnValue('visible');
    component['pendingUpdate'] = false;
    component.onVisibilityChange();

    expect(resizeSpy).toHaveBeenCalled();
  });

  it('should destroy chart on ngOnDestroy', () => {
    const destroySpy = spyOn(component.chart!, 'destroy').and.callThrough();
    component.ngOnDestroy();
    expect(destroySpy).toHaveBeenCalled();
    expect(component.chart).toBeNull();
  });
});
