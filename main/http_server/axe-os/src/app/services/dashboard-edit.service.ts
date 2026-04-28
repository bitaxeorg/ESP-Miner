import { Injectable } from '@angular/core';
import { BehaviorSubject, Subject } from 'rxjs';

export interface WidgetDef {
  id: string;
  label: string;
  x: number;
  y: number;
  w: number;
  h: number;
  minW?: number;
  minH?: number;
}

@Injectable({ providedIn: 'root' })
export class DashboardEditService {
  isActive$ = new BehaviorSubject<boolean>(false);

  editMode$ = new BehaviorSubject<boolean>(false);

  resetRequested$ = new Subject<void>();

  toggleWidgetRequested$ = new Subject<string>();

  widgetDefs: WidgetDef[] = [];

  hiddenWidgets = new Set<string>();

  toggleEditMode(): void {
    this.editMode$.next(!this.editMode$.value);
  }

  requestReset(): void {
    this.resetRequested$.next();
  }

  isWidgetVisible(id: string): boolean {
    return !this.hiddenWidgets.has(id);
  }
}
