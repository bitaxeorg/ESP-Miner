import { Component, OnInit, OnDestroy } from '@angular/core';
import { Observable, Subject, merge, switchMap, map, shareReplay, timer, takeUntil, finalize } from 'rxjs';
import { SystemService } from 'src/app/services/system.service';
import { LoadingService } from 'src/app/services/loading.service';
import { LocalStorageService } from 'src/app/local-storage.service';
import { ISystemScoreboardEntry } from 'src/models/ISystemScoreboard';

const SWARM_SORTING = 'SCOREBOARD_SORTING';

@Component({
  selector: 'app-scoreboard',
  templateUrl: './scoreboard.component.html',
  styleUrls: ['./scoreboard.component.scss']
})
export class ScoreboardComponent implements OnInit, OnDestroy {
  public scoreboard$: Observable<ISystemScoreboardEntry[]>;
  public sortField: string = '';
  public sortDirection: 'asc' | 'desc' = 'asc';

  private refresh$ = new Subject<void>();
  private destroy$ = new Subject<void>();

  constructor(
    private systemService: SystemService,
    private loadingService: LoadingService,
    private localStorageService: LocalStorageService,
  ) {
    const storedSorting = this.localStorageService.getObject(SWARM_SORTING) ?? {
      sortField: 'rank',
      sortDirection: 'asc'
    };
    this.sortField = storedSorting.sortField;
    this.sortDirection = storedSorting.sortDirection;

    this.scoreboard$ = merge(timer(0, 5000), this.refresh$).pipe(
      switchMap(() => this.systemService.getScoreboard().pipe(
        map(data => data.map((entry, index) => ({
          ...entry,
          rank: index,
          since: (Date.now() / 1000) - entry.ntime
        }))),
        map(data => this.sortData(data, this.sortField, this.sortDirection)),
        finalize(() => this.loadingService.loading$.next(false))
      )),
      shareReplay({refCount: true, bufferSize: 1}),
      takeUntil(this.destroy$)
    );
  }

  sortBy(field: string) {
    this.sortDirection = this.sortField === field
      ? this.sortDirection === 'asc' ? 'desc' : 'asc'
      : 'asc';
    this.sortField = field;

    this.localStorageService.setObject(SWARM_SORTING, {
      sortField: this.sortField,
      sortDirection: this.sortDirection
    });
    this.refresh$.next();
  }

  private sortData(data: ISystemScoreboardEntry[], field: string, direction: 'asc' | 'desc'): ISystemScoreboardEntry[] {
    if (!data || !field) {
      return data;
    }

    return data.sort((a, b) => {
      let valueA = a[field as keyof ISystemScoreboardEntry];
      let valueB = b[field as keyof ISystemScoreboardEntry];

      if (valueA < valueB) {
        return direction === 'asc' ? -1 : 1;
      } else if (valueA > valueB) {
        return direction === 'asc' ? 1 : -1;
      }
      return 0;
    });
  }

  ngOnInit() {
    this.loadingService.loading$.next(true);
  }

  ngOnDestroy() {
    this.destroy$.next();
    this.destroy$.complete();
  }
}
