import { CanActivateFn, Router } from '@angular/router';
import { inject } from '@angular/core';
import { Observable, map, catchError, of } from 'rxjs';
import { SystemService } from '../services/system.service';

export const ApModeGuard: CanActivateFn = (): Observable<boolean> => {
  const systemService = inject(SystemService);
  const router = inject(Router);

  return systemService.getInfo().pipe(
    map(info => {
      if (info.apEnabled) {
        router.navigate(['/ap']);
        return false;
      }
      return true;
    }),
    catchError(() => of(true))
  );
}; 