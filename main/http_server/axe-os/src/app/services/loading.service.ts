import { Injectable } from '@angular/core';
import { BehaviorSubject, Observable } from 'rxjs';

@Injectable({
  providedIn: 'root'
})
export class LoadingService {

  public loading$: BehaviorSubject<boolean> = new BehaviorSubject(false);

  constructor() { }

  public lockUIUntilComplete() {
    return <T>(source: Observable<T>): Observable<T> => {
      return new Observable(subscriber => {
        this.loading$.next(true);
        const subscription = source.subscribe({
          next: (value) => subscriber.next(value),
          error: (err) => {
            this.loading$.next(false);
            subscriber.error(err);
          },
          complete: () => {
            this.loading$.next(false);
            subscriber.complete();
          }
        });

        return () => {
          this.loading$.next(false);
          subscription.unsubscribe();
        };
      });
    }
  }
}
