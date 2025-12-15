import { Component, OnInit, OnDestroy } from '@angular/core';
import { Observable, Subject, interval, startWith, switchMap, takeUntil } from 'rxjs';
import { SystemService } from 'src/app/services/system.service';
import { LoadingService } from 'src/app/services/loading.service';
import { ISystemInfo } from 'src/models/ISystemInfo';

interface DifficultyRow {
  difficulty: number;
  label?: string;
  type?: 'fixed' | 'pool' | 'session' | 'best' | 'network' | 'expected';
  tooltip?: string;
  timeToFind: number;
  tenMin: string;
  hour: string;
  day: string;
  week: string;
  month: string;
  year: string;
}

@Component({
  selector: 'app-solo-chance',
  templateUrl: './solo-chance.component.html',
  styleUrls: ['./solo-chance.component.scss']
})
export class SoloChanceComponent implements OnInit, OnDestroy {
  public info$!: Observable<ISystemInfo>;
  public rows: DifficultyRow[] = [];
  private destroy$ = new Subject<void>();

  // Time periods in seconds
  private readonly TIME_10MIN = 600;
  private readonly TIME_HOUR  = this.TIME_10MIN * 6;
  private readonly TIME_DAY   = this.TIME_HOUR * 24;
  private readonly TIME_WEEK  = this.TIME_DAY * 7;
  private readonly TIME_YEAR  = this.TIME_DAY * 365.2425;
  private readonly TIME_MONTH = this.TIME_YEAR / 12;

  constructor(
    private systemService: SystemService,
    private loadingService: LoadingService
  ) {}

  ngOnInit(): void {
    this.loadingService.loading$.next(true);
    
    // Fetch immediately, then poll every 5 seconds like the dashboard
    this.info$ = interval(5000).pipe(
      startWith(0), // Emit immediately on subscription
      switchMap(() => this.systemService.getInfo()),
      takeUntil(this.destroy$)
    );
    
    this.info$.subscribe(info => {
      this.generateRows(info);
      this.loadingService.loading$.next(false);
    });
  }

  ngOnDestroy(): void {
    this.destroy$.next();
    this.destroy$.complete();
  }

  private generateRows(info: ISystemInfo): void {
    const hashRate = info.hashRate_1m; // in GH/s
    
    const difficulties: Array<{ value: number; label?: string; type: DifficultyRow['type']; tooltip?: string;  }> = [];
    
    let diff = 1;
    while (diff < (info.networkDifficulty ?? 1e14)) {
      difficulties.push({value: diff, type: 'fixed'});
      diff *= 1e3;
    }
    
    const expectedReachedDifficulty = this.calculateExpectedReachedDifficulty(hashRate, info.uptimeSeconds);
    if (expectedReachedDifficulty) {
      difficulties.push({
        value: expectedReachedDifficulty,
        label: '📊 Expected',
        type: 'expected',
        tooltip: 'Expected difficulty reached with current hashrate and uptime',
      });
    }

    // Add dynamic difficulties
    if (info.poolDifficulty > 0) {
      difficulties.push({ 
        value: info.poolDifficulty, 
        label: '🎯 Pool', 
        type: 'pool',
        tooltip: 'Your current pool difficulty setting',
      });
    }
    
    if (info.bestSessionDiff > 0) {
      difficulties.push({ 
        value: info.bestSessionDiff, 
        label: '🏆 Session Best', 
        type: 'session',
        tooltip: 'Best difficulty found since system boot',
      });
    }
    
    if (info.bestDiff > 0) {
      difficulties.push({ 
        value: info.bestDiff, 
        label: '🥇 All-time Best', 
        type: 'best',
        tooltip: 'Best difficulty ever found by this device',
      });
    }
    
    if (info.networkDifficulty && info.networkDifficulty > 0) {
      difficulties.push({ 
        value: info.networkDifficulty, 
        label: '🎰 Network', 
        type: 'network',
        tooltip: 'Current Bitcoin network difficulty (finding this = solo block!)',
      });
    }
    
    // Sort by difficulty value
    difficulties.sort((a, b) => a.value - b.value);
    
    // Remove duplicates - if a labeled row has the same difficulty as a fixed row, keep only the labeled one
    const uniqueDifficulties: Array<{ value: number; label?: string; tooltip?: string; type: DifficultyRow['type'] }> = [];
    const seenDifficulties = new Map<number, { value: number; label?: string; tooltip?: string; type: DifficultyRow['type'] }>();
    
    difficulties.forEach(item => {
      const existing = seenDifficulties.get(item.value);
      if (!existing) {
        // First time seeing this difficulty
        seenDifficulties.set(item.value, item);
      } else if (item.label && !existing.label) {
        // Replace unlabeled with labeled version
        seenDifficulties.set(item.value, item);
      }
      // If both have labels or both don't, keep the first one (existing)
    });
    
    seenDifficulties.forEach(item => uniqueDifficulties.push(item));
    
    // Generate rows
    this.rows = uniqueDifficulties.map(diff => {
      const timeToFind = this.calculateTimeToFind(diff.value, hashRate);
      
      return {
        difficulty: diff.value,
        label: diff.label,
        tooltip: diff.tooltip,
        type: diff.type,
        timeToFind,
        tenMin: this.formatProbability(diff.value, hashRate, this.TIME_10MIN),
        hour: this.formatProbability(diff.value, hashRate, this.TIME_HOUR),
        day: this.formatProbability(diff.value, hashRate, this.TIME_DAY),
        week: this.formatProbability(diff.value, hashRate, this.TIME_WEEK),
        month: this.formatProbability(diff.value, hashRate, this.TIME_MONTH),
        year: this.formatProbability(diff.value, hashRate, this.TIME_YEAR)
      };
    });
  }

  /**
   * Calculate expected time to find a share at given difficulty
   * @param difficulty Share difficulty
   * @param hashRate Hashrate in GH/s
   * @returns Time in seconds
   */
  private calculateTimeToFind(difficulty: number, hashRate: number): number {
    const hashesPerSecond = hashRate * 1e9; // Convert GH/s to H/s
    const expectedHashes = difficulty * Math.pow(2, 32);
    return expectedHashes / hashesPerSecond;
  }

  /**
   * Calculate expected number of occurrences in given time period
   * @param difficulty Share difficulty
   * @param hashRate Hashrate in GH/s
   * @param timeSeconds Time period in seconds
   * @returns Expected number of occurrences
   */
  private calculateExpectedOccurrences(difficulty: number, hashRate: number, timeSeconds: number): number {
    const hashesPerSecond = hashRate * 1e9;
    const expectedHashes = difficulty * Math.pow(2, 32);
    return (hashesPerSecond * timeSeconds) / expectedHashes;
  }

  /**
   * Format probability or occurrence count for display
   * @param difficulty Share difficulty
   * @param hashRate Hashrate in GH/s
   * @param timeSeconds Time period in seconds
   * @returns Formatted string
   */
  private formatProbability(difficulty: number, hashRate: number, timeSeconds: number): string {
    const expectedOccurrences = this.calculateExpectedOccurrences(difficulty, hashRate, timeSeconds);
    
    if (expectedOccurrences >= 1000000) {
      return `${(expectedOccurrences / 1e6).toFixed(1)}M×`;
    } else if (expectedOccurrences >= 1000) {
      return `${(expectedOccurrences / 1000).toFixed(1)}K×`;
    } else if (expectedOccurrences >= 100) {
      return `${Math.round(expectedOccurrences)}×`;
    } else if (expectedOccurrences >= 10) {
      return `${expectedOccurrences.toFixed(1)}×`;
    } else if (expectedOccurrences >= 1) {
      return `${expectedOccurrences.toFixed(2)}×`;
    } else {
      // Calculate probability using Poisson distribution
      const probability = 1 - Math.exp(-expectedOccurrences);
      
      if (probability >= 0.00000001) {
        return `${(probability * 100).toPrecision(2)}%`;
      } else {
        return '<0.000001%';
      }
    }
  }

  /**
   * Format duration in human-readable format
   * @param seconds Duration in seconds
   * @returns Formatted string
   */
  public formatDuration(seconds: number): string {
    if (seconds < 1) {
      return `${(seconds * 1000).toFixed(1)} ms`;
    } else if (seconds < 60) {
      return `${seconds.toFixed(1)} sec`;
    } else if (seconds < 3600) {
      return `${(seconds / 60).toFixed(1)} min`;
    } else if (seconds < 86400) {
      return `${(seconds / 3600).toFixed(1)} hours`;
    } else if (seconds < 31536000) {
      return `${(seconds / 86400).toFixed(1)} days`;
    } else {
      return `${(seconds / 31536000).toFixed(0)} years`;
    }
  }

  private calculateExpectedReachedDifficulty(hashRate: number, uptimeSeconds: number): number {
    const hashesPerSecond = hashRate * 1e9; // Convert GH/s to H/s
    const totalHashes = hashesPerSecond * uptimeSeconds;
    return totalHashes / Math.pow(2, 32);
  }
}
