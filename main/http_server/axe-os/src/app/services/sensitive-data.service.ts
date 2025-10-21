import { Injectable } from '@angular/core';
import { BehaviorSubject } from 'rxjs';
import { LocalStorageService } from 'src/app/local-storage.service';

const SENSITIVE_DATA_HIDDEN = 'SENSITIVE_DATA_HIDDEN';

@Injectable({ providedIn: 'root' })
export class SensitiveData {
  private hidden$: BehaviorSubject<boolean>;

  constructor(private localStorageService: LocalStorageService) {
    const storedState = this.localStorageService.getBool(SENSITIVE_DATA_HIDDEN);

    this.hidden$ = new BehaviorSubject<boolean>(storedState !== false);
  }

  get hidden() {
    return this.hidden$.asObservable();
  }

  toggle() {
    const newState = !this.hidden$.value;

    this.hidden$.next(newState);
    this.localStorageService.setBool(SENSITIVE_DATA_HIDDEN, newState);
  }
}
