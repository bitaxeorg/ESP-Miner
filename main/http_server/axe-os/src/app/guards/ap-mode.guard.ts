import { CanActivateFn, Router } from '@angular/router';
import { inject } from '@angular/core';
import { Observable, map, catchError, of } from 'rxjs';
import { SystemService } from '../generated/api/system.service';

export const ApModeGuard: CanActivateFn = (): Observable<boolean> => {
  const systemService = inject(SystemService);
  const router = inject(Router);

  return systemService.getSystemInfo().pipe(
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